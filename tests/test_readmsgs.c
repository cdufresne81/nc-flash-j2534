/* Unit test for PassThruReadMsgs: feeds crafted OpenPort USB transfers through a
   stubbed libusb. Build and run on Linux:
     gcc -Ij2534 $(pkg-config --cflags libusb-1.0) tests/test_readmsgs.c -o test_readmsgs && ./test_readmsgs
   (set TLOG=1 to print the driver log to stderr) */
#include "j2534.c"
#include <assert.h>
#include <unistd.h>
#include <signal.h>

static void on_alarm(int sig) { printf("HANG (no return within 5 s)\n"); fflush(stdout); _exit(2); }

typedef struct { const uint8_t *d; int n; } xfer_t;
static xfer_t q[8]; static int qn, qi, reads;

int LIBUSB_CALL libusb_bulk_transfer(libusb_device_handle *h, unsigned char ep,
	unsigned char *data, int length, int *transferred, unsigned int timeout)
{
	reads++;
	if (qi >= qn) { *transferred = 0; return LIBUSB_ERROR_TIMEOUT; }
	memcpy(data, q[qi].d, q[qi].n); *transferred = q[qi].n; qi++;
	return LIBUSB_SUCCESS;
}
const char * LIBUSB_CALL libusb_error_name(int e) { return "stub"; }
/* unused by ReadMsgs, present only to link */
int LIBUSB_CALL libusb_claim_interface(libusb_device_handle *h, int i) { return 0; }
void LIBUSB_CALL libusb_close(libusb_device_handle *h) {}
int LIBUSB_CALL libusb_detach_kernel_driver(libusb_device_handle *h, int i) { return 0; }
void LIBUSB_CALL libusb_exit(libusb_context *c) {}
void LIBUSB_CALL libusb_free_config_descriptor(struct libusb_config_descriptor *c) {}
void LIBUSB_CALL libusb_free_device_list(libusb_device **l, int u) {}
int LIBUSB_CALL libusb_get_config_descriptor(libusb_device *d, uint8_t i, struct libusb_config_descriptor **c) { return -1; }
uint8_t LIBUSB_CALL libusb_get_device_address(libusb_device *d) { return 0; }
int LIBUSB_CALL libusb_get_device_descriptor(libusb_device *d, struct libusb_device_descriptor *x) { return -1; }
ssize_t LIBUSB_CALL libusb_get_device_list(libusb_context *c, libusb_device ***l) { return 0; }
const struct libusb_version * LIBUSB_CALL libusb_get_version(void) { return NULL; }
int LIBUSB_CALL libusb_init(libusb_context **c) { return 0; }
int LIBUSB_CALL libusb_kernel_driver_active(libusb_device_handle *h, int i) { return 0; }
int LIBUSB_CALL libusb_open(libusb_device *d, libusb_device_handle **h) { return -1; }
int LIBUSB_CALL libusb_release_interface(libusb_device_handle *h, int i) { return 0; }

/* "ar6" packets: [a r ch len type ts0..3 payload...], len = 1 + 4 + payload */
static const uint8_t START_IND[] = { 'a','r','6', 9, 0x80, 0,0,0,1, 0,0,0x07,0xE8 };
static const uint8_t ARE[] = { 'a','r','e',' ','3','\r','\n' };
static const uint8_t ARO[] = { 'a','r','o','\r','\n' };
/* ISO15765 reply 7E8: 00 00 07 E8 76 01 (positive response to 36 01), as a data
   chunk then an end indication carrying the same CAN ID (stripped) */
static const uint8_t DATA_CHUNK[] = { 'a','r','6', 11, 0x00, 0,0,0,2, 0,0,0x07,0xE8, 0x76,0x01 };
static const uint8_t END_IND[]    = { 'a','r','6', 9, 0x40, 0,0,0,3, 0,0,0x07,0xE8 };

static PASSTHRU_MSG msgs[4];
static int run(const char *name, int nx, const xfer_t *x, unsigned long want_n, int32_t *rc_out)
{
	qn = nx; qi = 0; reads = 0;
	for (int i = 0; i < nx; i++) q[i] = x[i];
	unsigned long n = want_n;
	memset(msgs, 0, sizeof msgs);
	LAST_ERROR[0] = '\0';
	alarm(5);
	int32_t rc = PassThruReadMsgs(6, msgs, &n, 5000);
	alarm(0);
	printf("%-34s rc=%d n=%lu usb_reads=%d", name, rc, n, reads);
	if (n) printf(" msg0: RxStatus=%lu DataSize=%lu", msgs[0].RxStatus, msgs[0].DataSize);
	printf("  LAST_ERROR=\"%s\"\n", (char *)LAST_ERROR);
	*rc_out = rc;
	return (int)n;
}

int main(void)
{
	logfile = stderr; write_log = getenv("TLOG") != NULL;
	signal(SIGALRM, on_alarm);
	con->channel = '6'; con->protocol_id = 6; con->dev_handle = (void *)1;
	int32_t rc; int n, fails = 0;
#define CHECK(c) do { if (!(c)) { printf("   FAIL: %s\n", #c); fails++; } } while (0)

	n = run("1 start indication", 1, (xfer_t[]){{START_IND, sizeof START_IND}}, 1, &rc);
	CHECK(rc == 0 && n == 1 && msgs[0].RxStatus == 2);

	{ uint8_t b[64]; int k = 0; memcpy(b+k, DATA_CHUNK, sizeof DATA_CHUNK); k += sizeof DATA_CHUNK;
	  memcpy(b+k, END_IND, sizeof END_IND); k += sizeof END_IND;
	  n = run("2 reply (chunk + end ind)", 1, (xfer_t[]){{b, k}}, 1, &rc);
	  CHECK(rc == 0 && n == 1 && msgs[0].DataSize == 6 && msgs[0].Data[4] == 0x76); }

	{ uint8_t b[64]; int k = 0; memcpy(b+k, ARO, sizeof ARO); k += sizeof ARO;
	  memcpy(b+k, START_IND, sizeof START_IND); k += sizeof START_IND;
	  n = run("3 aro + start ind", 1, (xfer_t[]){{b, k}}, 1, &rc);
	  CHECK(rc == 0 && n == 1); }

	/* A cable error with no reply stays a timeout, as before: hosts (NC Flash)
	   retry a block on ERR_TIMEOUT and treat other codes as fatal. */
	n = run("4 are alone (then nothing)", 1, (xfer_t[]){{ARE, sizeof ARE}}, 1, &rc);
	CHECK(n == 0 && rc == J2534_ERR_TIMEOUT);

	n = run("4b are alone | start ind", 2,
		(xfer_t[]){{ARE, sizeof ARE}, {START_IND, sizeof START_IND}}, 1, &rc);
	CHECK(rc == 0 && n == 1 && msgs[0].RxStatus == 2);

	{ uint8_t b[64]; int k = 0; memcpy(b+k, ARE, sizeof ARE); k += sizeof ARE;
	  memcpy(b+k, START_IND, sizeof START_IND); k += sizeof START_IND;
	  n = run("5 are + start ind, same xfer", 1, (xfer_t[]){{b, k}}, 1, &rc);
	  CHECK(rc == 0 && n == 1 && msgs[0].RxStatus == 2); }   /* message not lost */

	{ uint8_t b[64]; int k = 0; memcpy(b+k, DATA_CHUNK, sizeof DATA_CHUNK); k += sizeof DATA_CHUNK;
	  memcpy(b+k, ARE, sizeof ARE); k += sizeof ARE;
	  memcpy(b+k, END_IND, sizeof END_IND); k += sizeof END_IND;
	  n = run("6 chunk + are + end ind", 1, (xfer_t[]){{b, k}}, 1, &rc);
	  CHECK(rc == 0 && n == 1 && msgs[0].DataSize == 6); }

	{ static const uint8_t ARE0[] = { 'a','r','e',' ','0','\r','\n' };
	  uint8_t b[64]; int k = 0; memcpy(b+k, ARE0, sizeof ARE0); k += sizeof ARE0;
	  memcpy(b+k, START_IND, sizeof START_IND); k += sizeof START_IND;
	  n = run("6b are 0 + start ind (not an error)", 1, (xfer_t[]){{b, k}}, 1, &rc);
	  CHECK(rc == 0 && n == 1); }

	n = run("7 nothing at all", 0, NULL, 1, &rc);
	CHECK(n == 0 && rc == J2534_ERR_TIMEOUT);

	/* An "are" arriving while a reply is still being assembled must not cut it:
	   the reply completes from the next transfer, and no stub follows. */
	{ uint8_t b[64]; int k = 0; memcpy(b+k, DATA_CHUNK, sizeof DATA_CHUNK); k += sizeof DATA_CHUNK;
	  memcpy(b+k, ARE, sizeof ARE); k += sizeof ARE;
	  n = run("8 chunk + are | end", 2, (xfer_t[]){{b, k}, {END_IND, sizeof END_IND}}, 1, &rc);
	  CHECK(rc == 0 && n == 1 && msgs[0].DataSize == 6 && msgs[0].Data[4] == 0x76);
	  n = run("8 ... next call", 0, NULL, 1, &rc);
	  CHECK(n == 0 && rc == J2534_ERR_TIMEOUT); }

	{ uint8_t b[64]; int k = 0; memcpy(b+k, ARO, sizeof ARO); k += sizeof ARO;
	  memcpy(b+k, DATA_CHUNK, sizeof DATA_CHUNK); k += sizeof DATA_CHUNK;
	  n = run("9 aro + chunk | are | end", 3,
		(xfer_t[]){{b, k}, {ARE, sizeof ARE}, {END_IND, sizeof END_IND}}, 1, &rc);
	  CHECK(rc == 0 && n == 1 && msgs[0].DataSize == 6 && msgs[0].Data[4] == 0x76);
	  n = run("9 ... next call", 0, NULL, 1, &rc);
	  CHECK(n == 0 && rc == J2534_ERR_TIMEOUT); }

	{ uint8_t b[64]; int k = 0; memcpy(b+k, START_IND, sizeof START_IND); k += sizeof START_IND;
	  memcpy(b+k, DATA_CHUNK, sizeof DATA_CHUNK); k += sizeof DATA_CHUNK;
	  memcpy(b+k, ARE, sizeof ARE); k += sizeof ARE;
	  n = run("10 start + chunk + are | end, want 2", 2, (xfer_t[]){{b, k}, {END_IND, sizeof END_IND}}, 2, &rc);
	  CHECK(rc == 0 && n == 2 && msgs[0].RxStatus == 2 && msgs[1].DataSize == 6 && msgs[1].Data[4] == 0x76);
	  n = run("10 ... next call", 0, NULL, 1, &rc);
	  CHECK(n == 0 && rc == J2534_ERR_TIMEOUT); }

	{ uint8_t b[64]; int k = 0; memcpy(b+k, START_IND, sizeof START_IND); k += sizeof START_IND;
	  memcpy(b+k, DATA_CHUNK, sizeof DATA_CHUNK); k += sizeof DATA_CHUNK;
	  memcpy(b+k, ARE, sizeof ARE); k += sizeof ARE;
	  n = run("11 start + chunk + are | end, want 1", 2, (xfer_t[]){{b, k}, {END_IND, sizeof END_IND}}, 1, &rc);
	  CHECK(rc == 0 && n == 1 && msgs[0].RxStatus == 2);
	  n = run("11 ... next call (queued reply)", 0, NULL, 1, &rc);
	  CHECK(rc == 0 && n == 1 && msgs[0].DataSize == 6 && msgs[0].Data[4] == 0x76); }

	/* unterminated "are 31" at the end of a transfer, reply in the next one */
	{ static const uint8_t ARE31[] = { 'a','r','e',' ','3','1' };
	  n = run("12 are 31 without CRLF | start ind", 2,
		(xfer_t[]){{ARE31, sizeof ARE31}, {START_IND, sizeof START_IND}}, 1, &rc);
	  CHECK(rc == 0 && n == 1 && msgs[0].RxStatus == 2); }

	/* An "are" right after a complete message must not start an empty queued
	   message (which used to drop the reply and return a stub next call). */
	{ uint8_t b[64]; int k = 0; memcpy(b+k, START_IND, sizeof START_IND); k += sizeof START_IND;
	  memcpy(b+k, ARE, sizeof ARE); k += sizeof ARE;
	  n = run("13 start ind + are, want 1", 1, (xfer_t[]){{b, k}}, 1, &rc);
	  CHECK(rc == 0 && n == 1 && msgs[0].RxStatus == 2);
	  n = run("13 ... next call", 0, NULL, 1, &rc);
	  CHECK(n == 0 && rc == J2534_ERR_TIMEOUT); }

	{ uint8_t b[64]; int k = 0; memcpy(b+k, DATA_CHUNK, sizeof DATA_CHUNK); k += sizeof DATA_CHUNK;
	  memcpy(b+k, END_IND, sizeof END_IND); k += sizeof END_IND;
	  memcpy(b+k, ARE, sizeof ARE); k += sizeof ARE;
	  n = run("14 reply + are, want 1", 1, (xfer_t[]){{b, k}}, 1, &rc);
	  CHECK(rc == 0 && n == 1 && msgs[0].DataSize == 6 && msgs[0].Data[4] == 0x76);
	  n = run("14 ... next call", 0, NULL, 1, &rc);
	  CHECK(n == 0 && rc == J2534_ERR_TIMEOUT); }

	/* A transfer ending in "ar", with a stale 'e' left in the buffer from an
	   earlier transfer: must not be taken for an "are" packet (used to hang). */
	{ static const uint8_t AR[] = { 'a','r' };
	  n = run("15 are | ar (truncated, stale e)", 2, (xfer_t[]){{ARE, sizeof ARE}, {AR, sizeof AR}}, 1, &rc);
	  CHECK(n == 0 && rc == J2534_ERR_TIMEOUT); }

	printf(fails ? "%d FAILED\n" : "ALL PASS\n", fails);
	return fails != 0;
}
