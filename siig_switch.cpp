#include <stdint.h>
#include <stdio.h>
#include <memory>
#include <vector>

#include <libusb.h>

static std::vector<libusb_device*> find_devices(libusb_context* ctx, int vendor_id, int product_id) {
  std::vector<libusb_device*> result;

  libusb_device** devices;
  ssize_t device_count = libusb_get_device_list(ctx, &devices);
  if (device_count < 0) {
    throw device_count;
  }

  for (ssize_t i = 0; i < device_count; ++i) {
    libusb_device* device = devices[i];
    struct libusb_device_descriptor descriptor;
    libusb_get_device_descriptor(device, &descriptor);
    if (descriptor.idVendor == vendor_id && descriptor.idProduct == product_id) {
      libusb_ref_device(device);
      result.push_back(device);
    }
  }

  libusb_free_device_list(devices, 1);
  return result;
}

class kvm_device {
 private:
  libusb_device* device = nullptr;
  libusb_device_handle* handle = nullptr;
  int interface_number;
  bool detached_kernel = false;

  kvm_device(libusb_device* device, int interface_number)
      : device(device), interface_number(interface_number) {
    // The reference count of the device has already been incremented.
    int rc = libusb_open(device, &handle);
    if (rc < 0) {
      fprintf(stderr, "libusb_open failed in kvm_device constructor\n");
      libusb_unref_device(device);
      throw rc;
    }

    rc = libusb_kernel_driver_active(handle, interface_number);
    if (rc < 0) {
      fprintf(stderr, "libusb_kernel_driver_active failed in kvm_device constructor\n");
      goto fail;
    } else if (rc) {
      rc = libusb_detach_kernel_driver(handle, interface_number);
      if (rc < 0) {
        goto fail;
      }
      detached_kernel = true;
    }

    rc = libusb_claim_interface(handle, interface_number);
    if (rc < 0) {
      fprintf(stderr, "libusb_claim_interface failed in kvm_device constructor\n");
      goto fail;
    }

    initialize();
    return;

  fail:
    if (detached_kernel) {
      libusb_attach_kernel_driver(handle, interface_number);
    }
    if (handle) {
      libusb_close(handle);
    }
    libusb_unref_device(device);

    throw rc;
  }

  kvm_device(const kvm_device& copy) = delete;
  kvm_device(kvm_device&& move) = delete;

 public:
  ~kvm_device() {
    if (detached_kernel) {
      libusb_attach_kernel_driver(handle, interface_number);
    }
    libusb_close(handle);
    libusb_unref_device(device);
  }

 private:
  int send_request(const unsigned char* data, size_t length, int timeout_ms = 100) {
    constexpr int CONTROL_REQUEST_TYPE_OUT =
      LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE;
    constexpr int HID_SET_REPORT = 0x09;
    constexpr int HID_REPORT_TYPE_OUTPUT = 0x02;

    int rc = libusb_control_transfer(handle, CONTROL_REQUEST_TYPE_OUT, HID_SET_REPORT,
                                     (HID_REPORT_TYPE_OUTPUT << 8) | 0x00, interface_number,
                                     const_cast<unsigned char*>(data), length, timeout_ms);
    if (rc < 0) {
      fprintf(stderr, "failed to write data in send_request\n");
      return rc;
    } else if (size_t(rc) != length) {
      fprintf(stderr, "libusb_control_transfer returned short, wrote %d, expected %zu\n", rc,
              length);
      return -1;
    }
    return 0;
  }

  void initialize() {
    constexpr unsigned char data[] = { 0x03, 0x00, 0x00, 0x00, 0x00 };
    int rc = send_request(data, sizeof(data));
    if (rc < 0) {
      throw rc;
    }
  }

 public:
  int trigger() {
    constexpr unsigned char data[] = { 0x03, 0x5c, 0x04, 0x00, 0x00 };
    return send_request(data, sizeof(data));
  }

  static constexpr int vendor_id = 0x2101;
  static constexpr int product_id = 0x1406;

  static std::unique_ptr<kvm_device> find_device(libusb_context* ctx = nullptr) {
    auto devices = find_devices(ctx, vendor_id, product_id);
    if (devices.size() == 0) {
      fprintf(stderr, "failed to find a connected KVM device\n");
      return nullptr;
    } else if (devices.size() > 1) {
      fprintf(stderr, "found multiple connected KVM devices\n");
      return nullptr;
    }

    libusb_device* device = devices[0];
    int interface_number = 0;

    {
      libusb_config_descriptor* config;
      int rc = libusb_get_active_config_descriptor(device, &config);
      if (rc) {
        fprintf(stderr, "failed to get active config descriptor: %s", libusb_error_name(rc));
        return nullptr;
      }

      constexpr size_t target_interface = 1;
      if (config->bNumInterfaces != 2) {
        fprintf(stderr, "unexpected number of interfaces: %u, expected 2", config->bNumInterfaces);
        return nullptr;
      }

      const libusb_interface& interfaces = config->interface[target_interface];
      if (interfaces.num_altsetting != 1) {
        fprintf(stderr, "unexpected number of alternate interfaces: %d, expected 1",
                interfaces.num_altsetting);
        return nullptr;
      }
      interface_number = interfaces.altsetting[0].bInterfaceNumber;
      libusb_free_config_descriptor(config);
    }

    return std::unique_ptr<kvm_device>(new kvm_device(device, interface_number));
  }
};

int main(int argc, const char* argv[]) {
  (void)argc;
  (void)argv;

  int rc = 0;
  libusb_context* ctx = nullptr;
  try {
    if (libusb_init(&ctx)) {
      fprintf(stderr, "error: failed to initialize libusb\n");
      return 1;
    }

    std::unique_ptr<kvm_device> kvm = kvm_device::find_device(ctx);
    if (kvm) {
      kvm->trigger();
    } else {
      rc = 1;
    }
  } catch (int libusb_error) {
    if (libusb_error) {
      fprintf(stderr, "libusb error: %s\n", libusb_error_name(libusb_error));
    }
    rc = 1;
  }

  if (ctx) {
    libusb_exit(ctx);
  }
  return rc;
}

// vim:expandtab ts=2 sts=2 sw=2
