#include <linux/module.h>
#include <linux/export-internal.h>
#include <linux/compiler.h>

MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(".gnu.linkonce.this_module") = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};



static const struct modversion_info ____versions[]
__used __section("__versions") = {
	{ 0x97dd6ca9, "ioremap" },
	{ 0xfad8f384, "iowrite32" },
	{ 0x1e017ae0, "i2c_register_driver" },
	{ 0x12ad300e, "iounmap" },
	{ 0xd7442be0, "class_destroy" },
	{ 0x0bc5fb0d, "unregister_chrdev_region" },
	{ 0xbd03ed67, "__ref_stack_chk_guard" },
	{ 0x5cb46e6d, "validate_usercopy_range" },
	{ 0xa61fd7aa, "__check_object_size" },
	{ 0x092a35a2, "_copy_from_user" },
	{ 0xf64ac983, "__copy_overflow" },
	{ 0xd272d446, "__stack_chk_fail" },
	{ 0x2435d559, "strncmp" },
	{ 0x62d8badb, "i2c_del_driver" },
	{ 0x73634a54, "cdev_del" },
	{ 0x408a0738, "device_destroy" },
	{ 0xd272d446, "__fentry__" },
	{ 0xd272d446, "__x86_return_thunk" },
	{ 0xe8213e80, "_printk" },
	{ 0x9f222e1e, "alloc_chrdev_region" },
	{ 0xfad798b2, "class_create" },
	{ 0x02106a3d, "device_create" },
	{ 0xd9324140, "cdev_init" },
	{ 0x45d300cd, "cdev_add" },
	{ 0xe9196a28, "module_layout" },
};

static const u32 ____version_ext_crcs[]
__used __section("__version_ext_crcs") = {
	0x97dd6ca9,
	0xfad8f384,
	0x1e017ae0,
	0x12ad300e,
	0xd7442be0,
	0x0bc5fb0d,
	0xbd03ed67,
	0x5cb46e6d,
	0xa61fd7aa,
	0x092a35a2,
	0xf64ac983,
	0xd272d446,
	0x2435d559,
	0x62d8badb,
	0x73634a54,
	0x408a0738,
	0xd272d446,
	0xd272d446,
	0xe8213e80,
	0x9f222e1e,
	0xfad798b2,
	0x02106a3d,
	0xd9324140,
	0x45d300cd,
	0xe9196a28,
};
static const char ____version_ext_names[]
__used __section("__version_ext_names") =
	"ioremap\0"
	"iowrite32\0"
	"i2c_register_driver\0"
	"iounmap\0"
	"class_destroy\0"
	"unregister_chrdev_region\0"
	"__ref_stack_chk_guard\0"
	"validate_usercopy_range\0"
	"__check_object_size\0"
	"_copy_from_user\0"
	"__copy_overflow\0"
	"__stack_chk_fail\0"
	"strncmp\0"
	"i2c_del_driver\0"
	"cdev_del\0"
	"device_destroy\0"
	"__fentry__\0"
	"__x86_return_thunk\0"
	"_printk\0"
	"alloc_chrdev_region\0"
	"class_create\0"
	"device_create\0"
	"cdev_init\0"
	"cdev_add\0"
	"module_layout\0"
;

MODULE_INFO(depends, "");

MODULE_ALIAS("i2c:ds1307");

MODULE_INFO(srcversion, "5FF9176685FA3B883C18D20");
