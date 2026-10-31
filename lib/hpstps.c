// hpstps.c - call \_SB.WMID.WMAA -> HWMC -> <command>; dump the returned status word
#include <linux/module.h>
#include <linux/acpi.h>
#include <linux/slab.h>

static int comd = 0x02;          /* 1 read, 2 write */
module_param(comd, int, 0644);
static int cmd = 0x4C;           /* CMDT */
module_param(cmd, int, 0644);
static int dszi = 1;
module_param(dszi, int, 0644);
static int d0 = 0, d1 = 0, d2 = 0, d3 = 0;
module_param(d0, int, 0644); module_param(d1, int, 0644);
module_param(d2, int, 0644); module_param(d3, int, 0644);
static int sizeclass = 1;
module_param(sizeclass, int, 0644);
static int quiet;
module_param(quiet, int, 0644);

static int __init hpstps_init(void)
{
	acpi_handle h = NULL;
	struct acpi_object_list arglist;
	union acpi_object args[3];
	struct acpi_buffer out = { ACPI_ALLOCATE_BUFFER, NULL };
	u8 *buf;
	acpi_status st;

	buf = kzalloc(0x100, GFP_KERNEL);
	if (!buf) return -ENOMEM;
	*(u32 *)(buf + 0x00) = 0x55434553;	/* SGIN "SECU" */
	*(u32 *)(buf + 0x04) = (u32)comd;
	*(u32 *)(buf + 0x08) = (u32)cmd;
	*(u32 *)(buf + 0x0C) = (u32)dszi;
	/* data area starts at BYTE 0x10: the ASL uses CreateField(Arg1, 0x80, ...)
	 * where 0x80 is a BIT offset */
	buf[0x10] = (u8)d0;
	buf[0x11] = (u8)d1;
	buf[0x12] = (u8)d2;
	buf[0x13] = (u8)d3;

	if (ACPI_FAILURE(acpi_get_handle(NULL, "\\_SB_.WMID", &h)) &&
	    ACPI_FAILURE(acpi_get_handle(NULL, "\\_SB.WMID", &h))) {
		pr_err("hpstps: no \\_SB.WMID\n");
		kfree(buf);
		return -ENODEV;
	}

	args[0].type = ACPI_TYPE_INTEGER; args[0].integer.value = 1;
	args[1].type = ACPI_TYPE_INTEGER; args[1].integer.value = sizeclass;
	args[2].type = ACPI_TYPE_BUFFER;
	args[2].buffer.length = 0x100;
	args[2].buffer.pointer = buf;
	arglist.count = 3;
	arglist.pointer = args;

	st = acpi_evaluate_object(h, "WMAA", &arglist, &out);
	if (ACPI_SUCCESS(st) && out.pointer) {
		union acpi_object *o = out.pointer;
		if (o->type == ACPI_TYPE_BUFFER && o->buffer.length >= 8) {
			u32 *p = (u32 *)o->buffer.pointer;
			pr_info("hpstps: COMD=%d CMD=0x%x DSZI=%d -> SIOU=0x%08x RETC=0x%x len=%u data=%*ph\n",
				comd, cmd, dszi, p[0], p[1], (u32)o->buffer.length,
				(int)min_t(u32, o->buffer.length - 8, 16),
				o->buffer.pointer + 8);
		} else if (o->type == ACPI_TYPE_PACKAGE) {
			u32 i;
			pr_info("hpstps: package %u elements\n", o->package.count);
			for (i = 0; i < o->package.count && i < 4; i++) {
				union acpi_object *e = &o->package.elements[i];
				if (e->type == ACPI_TYPE_INTEGER)
					pr_info("  [%u] int 0x%llx\n", i, e->integer.value);
				else if (e->type == ACPI_TYPE_BUFFER)
					pr_info("  [%u] buf %u: %*ph\n", i, (u32)e->buffer.length,
						(int)min_t(u32, e->buffer.length, 16), e->buffer.pointer);
			}
		} else {
			pr_info("hpstps: result type %u\n", o->type);
		}
	} else {
		pr_info("hpstps: COMD=%d CMD=0x%x -> %s\n", comd, cmd,
			acpi_format_exception(st));
	}
	if (out.pointer) kfree(out.pointer);
	kfree(buf);
	return 0;
}
static void __exit hpstps_exit(void) {}
module_init(hpstps_init); module_exit(hpstps_exit);
MODULE_LICENSE("GPL");
