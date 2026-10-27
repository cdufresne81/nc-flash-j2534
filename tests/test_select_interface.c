/* Unit test for the J2534 interface selection: select_j2534_interface() over
   descriptor structs, and PassThruOpen end to end through a stubbed libusb that
   enumerates the OpenPort without (0403:CC4D) and with (0403:CC4C) an SD card.
   Build and run on Linux:
     gcc -Ij2534 $(pkg-config --cflags libusb-1.0) tests/test_select_interface.c -o test_select_interface && ./test_select_interface */
#include "j2534.c"
#include <assert.h>
#include <unistd.h>
#include <signal.h>

static void on_alarm(int sig) { printf("HANG (no return within 5 s)\n"); fflush(stdout); _exit(2); }

/* Endpoint and interface descriptors, as dumped from a clone on fw 1.17.4877 */
static const struct libusb_endpoint_descriptor EP_ACM_INT[] = {
	{ .bEndpointAddress = 0x81, .bmAttributes = LIBUSB_TRANSFER_TYPE_INTERRUPT } };
static const struct libusb_endpoint_descriptor EP_DATA[] = {
	{ .bEndpointAddress = 0x02, .bmAttributes = LIBUSB_TRANSFER_TYPE_BULK },
	{ .bEndpointAddress = 0x82, .bmAttributes = LIBUSB_TRANSFER_TYPE_BULK } };
static const struct libusb_endpoint_descriptor EP_MSC[] = {
	{ .bEndpointAddress = 0x85, .bmAttributes = LIBUSB_TRANSFER_TYPE_BULK },
	{ .bEndpointAddress = 0x05, .bmAttributes = LIBUSB_TRANSFER_TYPE_BULK } };
static const struct libusb_endpoint_descriptor EP_TWO_IN[] = {
	{ .bEndpointAddress = 0x83, .bmAttributes = LIBUSB_TRANSFER_TYPE_BULK },
	{ .bEndpointAddress = 0x84, .bmAttributes = LIBUSB_TRANSFER_TYPE_BULK } };

#define INTF(num, cls, sub, proto, eps) { .bInterfaceNumber = num, .bAlternateSetting = 0, \
	.bNumEndpoints = sizeof eps / sizeof eps[0], .bInterfaceClass = cls, \
	.bInterfaceSubClass = sub, .bInterfaceProtocol = proto, .endpoint = eps }
static const struct libusb_interface_descriptor ACM0 = INTF(0, 0x02, 0x02, 0x01, EP_ACM_INT);
static const struct libusb_interface_descriptor DATA1 = INTF(1, 0x0a, 0x00, 0x00, EP_DATA);
static const struct libusb_interface_descriptor MSC2 = INTF(2, 0x08, 0x06, 0x50, EP_MSC);
static const struct libusb_interface_descriptor MSC0 = INTF(0, 0x08, 0x06, 0x50, EP_MSC);
static const struct libusb_interface_descriptor DATA_BAD = INTF(1, 0x0a, 0x00, 0x00, EP_TWO_IN);
static const struct libusb_interface_descriptor VENDOR1 = INTF(1, 0xff, 0x00, 0x00, EP_DATA);

#define IFACE(d) { .altsetting = &d, .num_altsetting = 1 }
static const struct libusb_interface L_CC4D[] = { IFACE(ACM0), IFACE(DATA1) };
static const struct libusb_interface L_CC4C[] = { IFACE(ACM0), IFACE(DATA1), IFACE(MSC2) };
static const struct libusb_interface L_MSC_FIRST[] = { IFACE(MSC0), IFACE(DATA1) };
static const struct libusb_interface L_NO_DATA[] = { IFACE(ACM0), IFACE(VENDOR1), IFACE(MSC2) };
static const struct libusb_interface L_DATA_TWO_IN[] = { IFACE(ACM0), IFACE(DATA_BAD), IFACE(MSC2) };
static const struct libusb_interface L_MSC_ONLY[] = { IFACE(MSC0) };

#define CONFIG(l) { .bNumInterfaces = sizeof l / sizeof l[0], .interface = l }
static const struct libusb_config_descriptor C_CC4D = CONFIG(L_CC4D);
static const struct libusb_config_descriptor C_CC4C = CONFIG(L_CC4C);
static const struct libusb_config_descriptor C_MSC_FIRST = CONFIG(L_MSC_FIRST);
static const struct libusb_config_descriptor C_NO_DATA = CONFIG(L_NO_DATA);
static const struct libusb_config_descriptor C_DATA_TWO_IN = CONFIG(L_DATA_TWO_IN);
static const struct libusb_config_descriptor C_MSC_ONLY = CONFIG(L_MSC_ONLY);

/* Stubbed libusb: a device list of (VID, PID, config) and a record of what
   PassThruOpen did with it */
typedef struct { uint16_t vid, pid; const struct libusb_config_descriptor *cfg; } fake_dev_t;
static fake_dev_t devs_tab[4]; static int n_devs;
static libusb_device *dev_ptrs[5];
static int opened = -1, claimed = -1, detached = -1, kactive_asked = -1, released = -1;
static int bad_ep_writes, writes;

static int dev_index(libusb_device *d) { return (int)((fake_dev_t *)d - devs_tab); }

ssize_t LIBUSB_CALL libusb_get_device_list(libusb_context *c, libusb_device ***l)
{
	for (int i = 0; i < n_devs; i++) dev_ptrs[i] = (libusb_device *)&devs_tab[i];
	dev_ptrs[n_devs] = NULL;
	*l = dev_ptrs;
	return n_devs;
}
int LIBUSB_CALL libusb_get_device_descriptor(libusb_device *d, struct libusb_device_descriptor *x)
{
	memset(x, 0, sizeof *x);
	x->idVendor = ((fake_dev_t *)d)->vid; x->idProduct = ((fake_dev_t *)d)->pid;
	return 0;
}
int LIBUSB_CALL libusb_get_config_descriptor(libusb_device *d, uint8_t i, struct libusb_config_descriptor **c)
{
	*c = (struct libusb_config_descriptor *)((fake_dev_t *)d)->cfg;
	return 0;
}
void LIBUSB_CALL libusb_free_config_descriptor(struct libusb_config_descriptor *c) {}
int LIBUSB_CALL libusb_open(libusb_device *d, libusb_device_handle **h)
{
	opened = dev_index(d); *h = (libusb_device_handle *)d; return 0;
}
uint8_t LIBUSB_CALL libusb_get_device_address(libusb_device *d) { return (uint8_t)(10 + dev_index(d)); }
int LIBUSB_CALL libusb_kernel_driver_active(libusb_device_handle *h, int i) { kactive_asked = i; return 1; }
int LIBUSB_CALL libusb_detach_kernel_driver(libusb_device_handle *h, int i) { detached = i; return 0; }
int LIBUSB_CALL libusb_claim_interface(libusb_device_handle *h, int i) { claimed = i; return 0; }
int LIBUSB_CALL libusb_release_interface(libusb_device_handle *h, int i) { released = i; return 0; }
int LIBUSB_CALL libusb_bulk_transfer(libusb_device_handle *h, unsigned char ep,
	unsigned char *data, int length, int *transferred, unsigned int timeout)
{
	writes++;
	if (ep != 0x02 && ep != 0x82) bad_ep_writes++;
	if (!(ep & LIBUSB_ENDPOINT_IN)) { *transferred = length; return 0; }
	static const char ack[] = "ari stub\r\naro\r\n";	/* answers ati, and acknowledges */
	memcpy(data, ack, sizeof ack - 1); *transferred = sizeof ack - 1;
	return 0;
}
const char * LIBUSB_CALL libusb_error_name(int e) { return "stub"; }
void LIBUSB_CALL libusb_close(libusb_device_handle *h) {}
void LIBUSB_CALL libusb_exit(libusb_context *c) {}
void LIBUSB_CALL libusb_free_device_list(libusb_device **l, int u) {}
const struct libusb_version * LIBUSB_CALL libusb_get_version(void) { return NULL; }
int LIBUSB_CALL libusb_init(libusb_context **c) { *c = NULL; return 0; }

static int fails;
#define CHECK(c) do { if (!(c)) { printf("   FAIL: %s\n", #c); fails++; } } while (0)

static void sel(const char *name, const struct libusb_config_descriptor *cfg,
	int want_r, int want_intf, int want_in, int want_out)
{
	endpoint_t ep = { 0xee, 0xee, 0xee };
	int r = select_j2534_interface(cfg, &ep);
	printf("select %-38s r=%d intf=%u in=0x%02X out=0x%02X\n", name, r, ep.intf_num, ep.addr_in, ep.addr_out);
	CHECK(r == want_r);
	if (want_r == LIBUSB_SUCCESS)
		CHECK(ep.intf_num == want_intf && ep.addr_in == want_in && ep.addr_out == want_out);
}

static int32_t open_with(const char *name, int n, const fake_dev_t *d, unsigned long *dev_id)
{
	n_devs = n; memcpy(devs_tab, d, n * sizeof *d);
	opened = claimed = detached = kactive_asked = released = -1;
	bad_ep_writes = writes = 0;
	memset(endpoint, 0, sizeof *endpoint);
	LAST_ERROR[0] = '\0';
	int32_t rc = PassThruOpen(NULL, dev_id);
	printf("open   %-38s rc=%d opened=%d claimed=%d detached=%d writes=%d bad_ep=%d LAST_ERROR=\"%s\"\n",
		name, rc, opened, claimed, detached, writes, bad_ep_writes, (char *)LAST_ERROR);
	return rc;
}

int main(void)
{
	logfile = stderr; write_log = FALSE;
	setvbuf(stdout, NULL, _IONBF, 0);
	signal(SIGALRM, on_alarm);
	alarm(5);

	/* pure selection over descriptors */
	sel("CC4D (no SD card)", &C_CC4D, LIBUSB_SUCCESS, 1, 0x82, 0x02);
	sel("CC4C (SD card): intf 1, not 2", &C_CC4C, LIBUSB_SUCCESS, 1, 0x82, 0x02);
	sel("mass storage listed first", &C_MSC_FIRST, LIBUSB_SUCCESS, 1, 0x82, 0x02);
	sel("no class 0x0A interface", &C_NO_DATA, LIBUSB_ERROR_NOT_FOUND, 0, 0, 0);
	sel("class 0x0A with two bulk IN", &C_DATA_TWO_IN, LIBUSB_ERROR_NOT_FOUND, 0, 0, 0);
	sel("mass storage only", &C_MSC_ONLY, LIBUSB_ERROR_NOT_FOUND, 0, 0, 0);

	/* PassThruOpen end to end */
	unsigned long id = 0;
	int32_t rc = open_with("CC4D", 1, (fake_dev_t[]){{0x0403, 0xcc4d, &C_CC4D}}, &id);
	CHECK(rc == J2534_NOERROR && opened == 0 && claimed == 1 && detached == 1 && kactive_asked == 1);
	CHECK(writes > 0 && bad_ep_writes == 0 && id == 10);
	CHECK(PassThruClose(id) == J2534_NOERROR && released == 1);

	rc = open_with("CC4C, behind an unrelated FTDI device", 2,
		(fake_dev_t[]){{0x0403, 0x6001, &C_CC4D}, {0x0403, 0xcc4c, &C_CC4C}}, &id);
	CHECK(rc == J2534_NOERROR && opened == 1 && claimed == 1 && detached == 1 && kactive_asked == 1);
	CHECK(writes > 0 && bad_ep_writes == 0 && id == 11);
	CHECK(PassThruClose(id) == J2534_NOERROR && released == 1);

	rc = open_with("CC4C without a CDC data interface", 1,
		(fake_dev_t[]){{0x0403, 0xcc4c, &C_NO_DATA}}, &id);
	CHECK(rc == J2534_ERR_DEVICE_NOT_CONNECTED && opened == -1 && claimed == -1 && detached == -1);
	CHECK(writes == 0 && strstr((char *)LAST_ERROR, "0403:CC4C") != NULL);
	CHECK(con->dev_handle == NULL);

	rc = open_with("no OpenPort", 1, (fake_dev_t[]){{0x1234, 0xcc4c, &C_CC4C}}, &id);
	CHECK(rc == J2534_ERR_DEVICE_NOT_CONNECTED && opened == -1 && writes == 0);

	printf(fails ? "%d FAILED\n" : "ALL PASS\n", fails);
	return fails != 0;
}
