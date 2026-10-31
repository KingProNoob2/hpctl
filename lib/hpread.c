// hpread.c - read ACPI integer objects relevant to GPU power
#include <linux/module.h>
#include <linux/acpi.h>

static const char *paths[] = {
	"\\_SB_.WMID.WTPS",
	"\\_SB_.PCI0.LPCB.EC0.ECOK",
	"\\_SB_.PCI0.LPCB.EC0.HPCM",
	"\\_SB_.PCI0.LPCB.EC0.DMST",
	"\\_SB_.PCI0.LPCB.EC0.DGTP",
	"\\_SB_.PCI0.LPCB.EC0.CLOW",
	"\\_SB_.PCI0.LPCB.EC0.CMAX",
	"\\_SB_.PCI0.LPCB.EC0.DPPC",
	"\\_SB_.PCI0.LPCB.EC0.GFXT",
	"\\_SB_.PCI0.LPCB.EC0.GPUT",
	"\\_SB_.PCI0.RP09.PEGP.GPTP",
	"\\_SB_.PCI0.RP09.PEGP.NLIM",
	"\\_SB_.PCI0.RP09.PEGP.GPFG",
	"\\_SB_.PCI0.LPCB.EC0.PSIN",
	"\\_SB_.PCI0.LPCB.EC0.STSA",
};

static int __init hpread_init(void)
{
	unsigned int i;
	for (i = 0; i < ARRAY_SIZE(paths); i++) {
		acpi_handle h = NULL;
		struct acpi_buffer out = { ACPI_ALLOCATE_BUFFER, NULL };
		acpi_status st;
		if (ACPI_FAILURE(acpi_get_handle(NULL, (char *)paths[i], &h))) {
			pr_info("hpread: %-32s <no handle>\n", paths[i]);
			continue;
		}
		st = acpi_evaluate_object(h, NULL, NULL, &out);
		if (ACPI_SUCCESS(st) && out.pointer) {
			union acpi_object *o = out.pointer;
			if (o->type == ACPI_TYPE_INTEGER)
				pr_info("hpread: %-32s = 0x%llx (%llu)\n", paths[i],
					o->integer.value, o->integer.value);
			else
				pr_info("hpread: %-32s type %u\n", paths[i], o->type);
		} else
			pr_info("hpread: %-32s err %s\n", paths[i], acpi_format_exception(st));
		if (out.pointer) kfree(out.pointer);
	}
	return 0;
}
static void __exit hpread_exit(void) {}
module_init(hpread_init); module_exit(hpread_exit);
MODULE_LICENSE("GPL");
