#include <stdio.h>
#include <hivex.h>
#include <unistd.h>
#include <bits/fcntl-linux.h>

#include "wtg.h"
#define EMSSETTINGS "{0ce4991b-e6b3-4b16-b23c-5e0d9250e5d9}"
#define RESUMELDRSETTINGS "{1afa9c49-16ab-4a5c-901b-212802da9460}"
#define DBGSETTINGS "{4636856e-540f-4170-a130-a84776f4c654}"

// #define RAMDEFECTS BADMEMORY
#define BADMEMORY "{5189b25c-5558-4bf2-bca4-289b11bd29e2}"

#define BOOTLDRSETTINGS "{6efb52bf-1766-41db-a6b3-0ee5eff72bd7}"

#define GLOBALSETTINGS "{7ea2e1ac-2e61-4728-aaa3-896d9d0a9f0e}"
#define HYPERVISORSETTINGS "{7ff607e0-4395-11db-b0de-0800200c9a66}"

#define BOOTMGR "{9dea862c-5cdd-4e70-acc1-f32b344d4795}"

// #define MEMTESTER MEMDIAG
#define MEMDIAG "{b2721d73-1db4-4c62-bf78-c548a880142d}"

#define WINRESUME_EFI_TEMPLATE "{0c334284-9a41-4de1-99b3-a7e87e8ff07e}"
#define NTLDR "{466f5a88-0af2-4f76-9038-095b170dc21c}"
#define SETUP_EFI "{7254a080-1510-4e85-ac0f-e7fb3d444736}"
#define WINRESUME_PCAT_TEMPLATE "{98b02a23-0674-4ce7-bdad-e0a15a8ff97b}"
#define OS_TARGET_PCAT_TEMPLATE "{a1943bbc-ea85-487c-97c7-c9ede908a38a}"
#define OS_TARGET_EFI_TEMPLATE "{b012b84d-c47c-4ed5-b722-c0c42163e569}"
#define SETUP_PCAT "{cbd971bf-b7b8-4885-951a-fa03044f5d71}"

// TODO: Generate these two randomly
static char winload_guid[] = "{32fd51e9-5860-4cec-a1cd-2a05f1791589}";
static char winresume_guid[] = "{d214db63-7804-4a92-830e-df46da1915fe}";


static char boot_partition_bytes[88] = {[16] = 0x06, [24] = 0x48};
static char esp_bytes[88] = {[16] = 0x06, [24] = 0x48};


static char locale[] = "en-US";

static char* faaltu[7] = {
	WINRESUME_EFI_TEMPLATE, NTLDR, SETUP_EFI, WINRESUME_PCAT_TEMPLATE, OS_TARGET_PCAT_TEMPLATE,
	OS_TARGET_EFI_TEMPLATE, SETUP_PCAT
};

// https://devblogs.microsoft.com/oldnewthing/20091008-00/?p=16443


static void insertval(hive_h *handle, hive_node_h node_elem_h, char *nodename, hive_type type, int size, char data[])
{
	hive_node_h node_elem_val_h = 0;
	node_elem_val_h = hivex_node_get_child(handle, node_elem_h, nodename);

	if(node_elem_val_h == 0) {
		node_elem_val_h = hivex_node_add_child(handle, node_elem_h, nodename);
	}

	hive_set_value node_elem_val_elem = {
			.key = "Element",
			.t = type,
			.len = size,
			.value = data};
	hivex_node_set_value(handle, node_elem_val_h, &node_elem_val_elem, 0);
}

static void setdesc(hive_h *handle, hive_node_h node_h, char bytes[])
{
	hive_node_h node_desc_h = hivex_node_add_child(handle, node_h, "Description");
	hive_set_value node_desc_type = {
			.key = "Type",
			.t = hive_t_REG_DWORD,
			.len = 4,
			.value = bytes,
	};
	hivex_node_set_value(handle, node_desc_h, &node_desc_type, 0);
}

static void get_utf16le(char *src, char dest[], int size)
{
	for (int i = 0, j = 0; i < size && src[j] != '\0'; i += 2, j++)
	{
		dest[i] = src[j];
	}
}


static void setbytearray(char dest[88], char disk_bits[16], char part_bits[16]){
	for(int i = 0; i<16;i++){
		dest[i+32] = part_bits[i];
		dest[i+56] = disk_bits[i];
	}
}


int createBootBCD(const char* path, char disk_bits[16], char esp_part_bits[16], char boot_part_bits[16])
{
	setbytearray(boot_partition_bytes, disk_bits, boot_part_bits);
	setbytearray(esp_bytes, disk_bits, esp_part_bits);


	int error = faccessat(-1, path, F_OK, AT_EACCESS);

	if (error) {
		printf("BCD-Template not found. Failed to create BCD entries. Media will be unbootable\n");
		return error;
	}

	hive_h *handle = hivex_open(path, HIVEX_OPEN_WRITE);

	hive_node_h root = hivex_root(handle);

	hive_node_h desc = hivex_node_get_child(handle, root, "Description");

	char fmodbytes[] = {0x01, 0x00, 0x00, 0x00};
	hive_set_value fmod = {
			.key = "FirmwareModified",
			.t = hive_t_REG_DWORD,
			.len = 4,
			.value = fmodbytes};
	hivex_node_set_value(handle, desc, &fmod, 0);

	char systummbytes[] = {0x01, 0x00, 0x00, 0x00};
	hive_set_value systumm = {
			.key = "System",
			.t = hive_t_REG_DWORD,
			.len = 4,
			.value = systummbytes};
	hivex_node_set_value(handle, desc, &systumm, 0);


	char keyNamebytes[24] = {0};
	get_utf16le("BCD00000000", keyNamebytes, 24);
	hive_set_value keyName = {
		.key = "KeyName",
		.t = hive_t_REG_SZ,
		.len = 24,
		.value = keyNamebytes
	};

	hivex_node_set_value(handle, desc, &keyName, 0);


	hive_node_h objs = hivex_node_get_child(handle, root, "Objects");



	char *node0 = EMSSETTINGS;
	//hive_node_h node0_h = hivex_node_add_child(handle, objs, node0);
	//hive_node_h node0_elem_h = hivex_node_add_child(handle, node0_h, "Elements");
	hive_node_h node0_h = hivex_node_get_child(handle, objs, node0);
	hive_node_h node0_elem_h = hivex_node_get_child(handle, node0_h, "Elements");

	//setdesc(handle, node0_h, (char[]){0x00, 0x00, 0x10, 0x20});

	{
		int size = 1;
		char bt16000020[1] = {0};
		insertval(handle, node0_elem_h, "16000020", hive_t_REG_BINARY, size, bt16000020);
	}

	char *node1 = RESUMELDRSETTINGS;
	hive_node_h node1_h = hivex_node_get_child(handle, objs, node1);
	hive_node_h node1_elem_h = hivex_node_get_child(handle, node1_h, "Elements");

	// char node1_desc_type_bytes[] = {0x04, 0x00, 0x20, 0x20};
	// hive_set_value node1_desc_type = {
	// 		.key = "Type",
	// 		.t = hive_t_REG_DWORD,
	// 		.len = 4,
	// 		.value = node1_desc_type_bytes};
	// hivex_node_set_value(handle, node1_desc_h, &node1_desc_type, 0);

	//setdesc(handle, node1_h, (char[]){0x04, 0x00, 0x20, 0x20});

	{	// TODO: This is redundant
		int size = 80;
		char bt14000006[80] = {0};
		get_utf16le(GLOBALSETTINGS, bt14000006, size);
		insertval(handle, node1_elem_h, "14000006", hive_t_REG_MULTI_SZ, size, bt14000006);
	}

	char *node2 = winload_guid;
	hive_node_h node2_h = hivex_node_add_child(handle, objs, node2);
	hive_node_h node2_elem_h = hivex_node_add_child(handle, node2_h, "Elements");

	setdesc(handle, node2_h, (char[]){0x03, 0x00, 0x20, 0x10});

	{
		char *bt11000001 = boot_partition_bytes;
		insertval(handle, node2_elem_h, "11000001", hive_t_REG_BINARY, 88, bt11000001);
	}

	{
		const int size = 60;
		// Why can't I do this? C is stupid :(
		// char bt12000002[size] = {0};

		char bt12000002[60] = {0};
		get_utf16le("\\Windows\\system32\\winload.efi", bt12000002, size);
		insertval(handle, node2_elem_h, "12000002", hive_t_REG_SZ, size, bt12000002);
	}
	{
		const int size = 22;
		char bt12000004[22] = {0};
		get_utf16le("Windows 10", bt12000004, size);
		insertval(handle, node2_elem_h, "12000004", hive_t_REG_SZ, size, bt12000004);
	}
	{
		const int size = 12;
		char bt12000005[12] = {0};
		get_utf16le(locale, bt12000005, size);
		insertval(handle, node2_elem_h, "12000005", hive_t_REG_SZ, size, bt12000005);
	}
	{
		const int size = 80;
		char bt14000006[80] = {0};
		get_utf16le(BOOTLDRSETTINGS, bt14000006, size);
		insertval(handle, node2_elem_h, "14000006", hive_t_REG_MULTI_SZ, size, bt14000006);
	}
	{
		const int size = 1;
		char bt16000060[1] = {1};
		insertval(handle, node2_elem_h, "16000060", hive_t_REG_BINARY, size, bt16000060);
	}
	{
		const int size = 8;
		char bt17000077[8] = {0x75, 0x00, 0x00, 0x15, 0x00, 0x00, 0x00, 0x00};
		insertval(handle, node2_elem_h, "17000077", hive_t_REG_BINARY, size, bt17000077);
	}
	{
		const int size = 88;
		char *bt21000001 = boot_partition_bytes;
		insertval(handle, node2_elem_h, "21000001", hive_t_REG_BINARY, size, bt21000001);
	}

	{
		const int size = 18;
		char bt22000002[18] = {0};
		get_utf16le("\\Windows", bt22000002, size);
		insertval(handle, node2_elem_h, "22000002", hive_t_REG_SZ, size, bt22000002);
	}

	{
		const int size = 78;
		char bt23000003[78] = {0};
		get_utf16le(winresume_guid, bt23000003, size);
		insertval(handle, node2_elem_h, "23000003", hive_t_REG_SZ, size, bt23000003);
	}

	{
		const int size = 8;
		char bt25000020[8] = {0};
		insertval(handle, node2_elem_h, "25000020", hive_t_REG_BINARY, size, bt25000020);
	}

	{
		const int size = 8;
		char bt250000c2[8] = {0x01};
		insertval(handle, node2_elem_h, "250000c2", hive_t_REG_BINARY, size, bt250000c2);
	}

	char *node3a = DBGSETTINGS;
	hive_node_h node3a_h = hivex_node_add_child(handle, objs, node3a);
	hive_node_h node3a_elem_h = hivex_node_add_child(handle, node3a_h, "Elements");

	//setdesc(handle, node3a_h, (char[]){0x00, 0x00, 0x10, 0x20});

	// TODO: Delete template key here
	{
		int size = 8;
		char bt15000011[8] = {0x04};
		insertval(handle, node3a_elem_h, "15000011", hive_t_REG_BINARY, size, bt15000011);
	}



	// char *node3b = BADMEMORY;
	// hive_node_h node3b_h = hivex_node_add_child(handle, objs, node3b);
	// hive_node_h node3b_elem_h = hivex_node_add_child(handle, node3b_h, "Elements");

	//setdesc(handle, node3b_h, (char[]){0x00, 0x00, 0x10, 0x20});



	// LMAO after all that effort, turns out this is f#¢k1ng reduntant
	// char *node3 = BOOTLDRSETTINGS;
	// hive_node_h node3_h = hivex_node_add_child(handle, objs, node3);
	// hive_node_h node3_elem_h = hivex_node_add_child(handle, node3_h, "Elements");
	//
	// setdesc(handle, node3_h, (char[]){0x03, 0x00, 0x20, 0x20});
	//
	// {
	// 	const int size = 158;
	// 	char bt14000006[158] = {0};
	// 	get_utf16le(GLOBALSETTINGS, bt14000006, 76);
	// 	// Jugaad for weird string format
	// 	get_utf16le(HYPERVISORSETTINGS, bt14000006 + 78, 76);
	// 	insertval(handle, node3_elem_h, "14000006", hive_t_REG_MULTI_SZ, size, bt14000006);
	// }


	// What ra sudeep, too much redundancy you are showing
	//
	// char *node4 = GLOBALSETTINGS; // Used in node1_elem_14000006_elem
	// hive_node_h node4_h = hivex_node_add_child(handle, objs, node4);
	// hive_node_h node4_elem_h = hivex_node_add_child(handle, node4_h, "Elements");
	//
	// setdesc(handle, node4_h, (char[]){0x00, 0x00, 0x10, 0x20});
	//
	// {
	// 	const int size = 236;
	// 	char bt14000006[236] = {0};
	// 	get_utf16le(DBGSETTINGS, bt14000006, 76);
	// 	// Turiksu for weird string format
	// 	get_utf16le(EMSSETTINGS, bt14000006 + 78, 76);
	// 	// LMAO yeh chal kyun raha hai 😂😂
	// 	get_utf16le(BADMEMORY, bt14000006 + 78 + 76 + 2, 76);
	// 	insertval(handle, node4_elem_h, "14000006", hive_t_REG_MULTI_SZ, size, bt14000006);
	// }

	// TODO: Delete template key
	// Moye moye ho gaya :(
	// char *node5a = HYPERVISORSETTINGS;
	// hive_node_h node5a_h = hivex_node_add_child(handle, objs, node5a);
	// hive_node_h node5a_elem_h = hivex_node_add_child(handle, node5a_h, "Elements");

	// setdesc(handle, node5a_h, (char[]){0x03, 0x00, 0x20, 0x20});

	// {
	// 	int size = 8;
	// 	char bt250000f3[8] = {0};
	// 	insertval(handle, node5a_elem_h, "250000f3", hive_t_REG_BINARY, size, bt250000f3);
	// }
	// {
	// 	int size = 8;
	// 	char bt250000f4[8] = {0x01};
	// 	insertval(handle, node5a_elem_h, "250000f4", hive_t_REG_BINARY, size, bt250000f4);
	// }
	// {
	// 	int size = 8;
	// 	char bt250000f5[8] = {[1] = 0xC2, [2] = 0x01};
	// 	insertval(handle, node5a_elem_h, "250000f5", hive_t_REG_BINARY, size, bt250000f5);
	// }

	char *node5 = BOOTMGR;
	hive_node_h node5_h = hivex_node_get_child(handle, objs, node5);
	hive_node_h node5_elem_h = hivex_node_get_child(handle, node5_h, "Elements");

	//setdesc(handle, node5_h, (char[]){0x02, 0x00, 0x10, 0x10});

	{
		const int size = 88;
		char* bt11000001 = esp_bytes;
		insertval(handle, node5_elem_h, "11000001", hive_t_REG_BINARY, size, bt11000001);
	}
	// Bhai kya kar raha hai tu? Mazak hai kya?
	// {
	// 	const int size = 66;
	// 	char bt12000002[66] = {0};
	// 	get_utf16le("\\EFI\\Microsoft\\Boot\\bootmgfw.efi", bt12000002, size);
	// 	insertval(handle, node5_elem_h, "12000002", hive_t_REG_SZ, size, bt12000002);
	// }
	// Yeh sab doglapan hai
	// {
	// 	const int size = 42;
	// 	char bt12000004[42] = {0};
	// 	get_utf16le("Windows Boot Manager", bt12000004, size);
	// 	insertval(handle, node5_elem_h, "12000004", hive_t_REG_SZ, size, bt12000004);
	// }
	// TODO: Maybe do locale setting like Rufus
	// {
	// 	const int size = 12;
	// 	char bt12000005[12] = {0};
	// 	get_utf16le(locale, bt12000005, size);
	// 	insertval(handle, node5_elem_h, "12000005", hive_t_REG_SZ, size, bt12000005);
	// }
	// Maine kaha... Hypocrisy ki bhi seema hoti hai...
	// {
	// 	const int size = 80;
	// 	char bt14000006[80] = {0};
	// 	get_utf16le(GLOBALSETTINGS, bt14000006, size);
	// 	insertval(handle, node5_elem_h, "14000006", hive_t_REG_MULTI_SZ, size, bt14000006);
	// }
	{
		const int size = 78;
		char bt23000003[78] = {0};
		get_utf16le(winload_guid, bt23000003, size);
		insertval(handle, node5_elem_h, "23000003", hive_t_REG_SZ, size, bt23000003);
	}
	{
		const int size = 78;
		char bt23000006[78] = {0};
		get_utf16le(winresume_guid, bt23000006, size);
		insertval(handle, node5_elem_h, "23000006", hive_t_REG_SZ, size, bt23000006);
	}
	{
		const int size = 80;
		char bt24000001[80] = {0};
		get_utf16le(winload_guid, bt24000001, size);
		insertval(handle, node5_elem_h, "24000001", hive_t_REG_MULTI_SZ, size, bt24000001);
	}
	{
		const int size = 80;
		char bt24000010[80] = {0};
		get_utf16le(MEMDIAG, bt24000010, size);
		insertval(handle, node5_elem_h, "24000010", hive_t_REG_MULTI_SZ, size, bt24000010);
	}
	// Moye moye part 2?
	// {
	// 	const int size = 8;
	// 	char bt25000004[8] = {0x1E};
	// 	insertval(handle, node5_elem_h, "25000004", hive_t_REG_BINARY, size, bt25000004);
	// }

	char *node6 = MEMDIAG;
	hive_node_h node6_h = hivex_node_add_child(handle, objs, node6);
	hive_node_h node6_elem_h = hivex_node_add_child(handle, node6_h, "Elements");

	//setdesc(handle, node6_h, (char[]){0x05, 0x00, 0x20, 0x10});

	{
		const int size = 88;
		char* bt11000001 = esp_bytes;
		insertval(handle, node6_elem_h, "11000001", hive_t_REG_BINARY, size, bt11000001);
	}
	{
		const int size = 64;
		char bt12000002[64] = {0};
		get_utf16le("\\EFI\\Microsoft\\Boot\\memtest.efi", bt12000002, size);
		insertval(handle, node6_elem_h, "12000002", hive_t_REG_SZ, size, bt12000002);
	}
	{
		const int size = 52;
		char bt12000004[52] = {0};
		get_utf16le("Windows Memory Diagnostic", bt12000004, size);
		insertval(handle, node6_elem_h, "12000004", hive_t_REG_SZ, size, bt12000004);
	}
	// TODO: Maybe do locale setting like Rufus
	// {
	// 	const int size = 12;
	// 	char bt12000005[12] = {0};
	// 	get_utf16le(locale, bt12000005, size);
	// 	insertval(handle, node6_elem_h, "12000005", hive_t_REG_SZ, size, bt12000005);
	// }
	// {
	// 	const int size = 80;
	// 	char bt14000006[80] = {0};
	// 	get_utf16le(GLOBALSETTINGS, bt14000006, size);
	// 	insertval(handle, node6_elem_h, "14000006", hive_t_REG_MULTI_SZ, size, bt14000006);
	// }
	// {
	// 	const int size = 1;
	// 	char bt1600000b[1] = {0x01};
	// 	insertval(handle, node6_elem_h, "1600000b", hive_t_REG_BINARY, size, bt1600000b);
	// }



	char *node7 = winresume_guid;
	hive_node_h node7_h = hivex_node_add_child(handle, objs, node7);
	hive_node_h node7_elem_h = hivex_node_add_child(handle, node7_h, "Elements");

	setdesc(handle, node7_h, (char[]){0x04, 0x00, 0x20, 0x10});

	{
		const int size = 88;
		char *bt11000001 = boot_partition_bytes;
		insertval(handle, node7_elem_h, "11000001", hive_t_REG_BINARY, size, bt11000001);
	}
	{
		const int size = 64;
		char bt12000002[64] = {0};
		get_utf16le("\\Windows\\system32\\winresume.efi", bt12000002, size);
		insertval(handle, node7_elem_h, "12000002", hive_t_REG_SZ, size, bt12000002);
	}
	{
		const int size = 54;
		char bt12000004[54] = {0};
		get_utf16le("Windows Resume Application", bt12000004, size);
		insertval(handle, node7_elem_h, "12000004", hive_t_REG_SZ, size, bt12000004);
	}
	{
		const int size = 12;
		char bt12000005[12] = {0};
		get_utf16le(locale, bt12000005, size);
		insertval(handle, node7_elem_h, "12000005", hive_t_REG_SZ, size, bt12000005);
	}
	{
		const int size = 80;
		char bt14000006[80] = {0};
		get_utf16le(RESUMELDRSETTINGS, bt14000006, size);
		insertval(handle, node7_elem_h, "14000006", hive_t_REG_MULTI_SZ, size, bt14000006);
	}
	{
		const int size = 1;
		char bt16000060[1] = {0x01};
		insertval(handle, node7_elem_h, "16000060", hive_t_REG_BINARY, size, bt16000060);
	}
	{
		const int size = 8;
		char bt17000077[8] = {0x75, 0x00, 0x00, 0x15, 0x00, 0x00, 0x00, 0x00};
		insertval(handle, node7_elem_h, "17000077", hive_t_REG_BINARY, size, bt17000077);
	}
	{
		const int size = 88;
		char *bt21000001 = boot_partition_bytes;
		insertval(handle, node7_elem_h, "21000001", hive_t_REG_BINARY, size, bt21000001);
	}
	{
		const int size = 28;
		char bt22000002[28] = {0};
		get_utf16le("\\hiberfil.sys", bt22000002, size);
		insertval(handle, node7_elem_h, "22000002", hive_t_REG_SZ, size, bt22000002);
	}
	{
		const int size = 8;
		char bt25000008[8] = {0x01};
		insertval(handle, node7_elem_h, "25000008", hive_t_REG_BINARY, size, bt25000008);
	}

	for (int i = 0; i<7;i++) {
		char *node = faaltu[i];
		hive_node_h node_h = hivex_node_get_child(handle, objs, node);
		hivex_node_delete_child(handle, node_h);
	}


	// printf("%s\n", hivex_value_key(handle, descarr[0]));
	// size_t len = 0;
	// hive_type type;
	// hivex_value_type(handle, descarr[0], &type,&len);
	// printf("Len: %d, Type: %d", len, type);

	// printf("%s\n", hivex_node_name(handle, desc));

	// hive_node_h *children = hivex_node_children(handle, root);
	// int size = hivex_node_nr_children(handle, root);
	// for (int i = 0; i < size; i++)
	// {
	// 	char *name = hivex_node_name(handle, children[i]);
	// 	printf("%s\n", name);
	// }

	hivex_commit(handle, NULL, 0);
	hivex_close(handle);

	return 0;
}


int createRecBCD(char disk_bits[16], char esp_part_bits[16])
{
	setbytearray(esp_bytes, disk_bits, esp_part_bits);


	hive_h *handle = hivex_open("cerBCD", HIVEX_OPEN_WRITE);

	hive_node_h root = hivex_root(handle);

	hive_node_h desc = hivex_node_get_child(handle, root, "Description");

	char fmodbytes[] = {0x01, 0x00, 0x00, 0x00};
	hive_set_value fmod = {
			.key = "FirmwareModified",
			.t = hive_t_REG_DWORD,
			.len = 4,
			.value = fmodbytes};
	hivex_node_set_value(handle, desc, &fmod, 0);


	char keyNamebytes[24] = {0};
	get_utf16le("BCD00000001", keyNamebytes, 24);
	hive_set_value keyName = {
		.key = "KeyName",
		.t = hive_t_REG_SZ,
		.len = 24,
		.value = keyNamebytes
	};
	hivex_node_set_value(handle, desc, &keyName, 0);



	hive_node_h objs = hivex_node_get_child(handle, root, "Objects");


	char *node0 = EMSSETTINGS;
	hive_node_h node0_h = hivex_node_add_child(handle, objs, node0);
	hive_node_h node0_elem_h = hivex_node_add_child(handle, node0_h, "Elements");

	setdesc(handle, node0_h, (char[]){0x00, 0x00, 0x10, 0x20});

	{
		int size = 1;
		char bt16000020[1] = {0};
		insertval(handle, node0_elem_h, "16000020", hive_t_REG_BINARY, size, bt16000020);
	}

	char *node1 = RESUMELDRSETTINGS;
	hive_node_h node1_h = hivex_node_add_child(handle, objs, node1);
	hive_node_h node1_elem_h = hivex_node_add_child(handle, node1_h, "Elements");

	// char node1_desc_type_bytes[] = {0x04, 0x00, 0x20, 0x20};
	// hive_set_value node1_desc_type = {
	// 		.key = "Type",
	// 		.t = hive_t_REG_DWORD,
	// 		.len = 4,
	// 		.value = node1_desc_type_bytes};
	// hivex_node_set_value(handle, node1_desc_h, &node1_desc_type, 0);
	setdesc(handle, node1_h, (char[]){0x04, 0x00, 0x20, 0x20});

	{
		int size = 80;
		char bt14000006[80] = {0};
		get_utf16le(GLOBALSETTINGS, bt14000006, size);
		insertval(handle, node1_elem_h, "14000006", hive_t_REG_MULTI_SZ, size, bt14000006);
	}

	char *node3a = DBGSETTINGS;
	hive_node_h node3a_h = hivex_node_add_child(handle, objs, node3a);
	hive_node_h node3a_elem_h = hivex_node_add_child(handle, node3a_h, "Elements");

	setdesc(handle, node3a_h, (char[]){0x00, 0x00, 0x10, 0x20});

	{
		int size = 8;
		char bt15000011[8] = {0x04};
		insertval(handle, node3a_elem_h, "15000011", hive_t_REG_BINARY, size, bt15000011);
	}



	char *node3b = BADMEMORY;
	hive_node_h node3b_h = hivex_node_add_child(handle, objs, node3b);
	hive_node_h node3b_elem_h = hivex_node_add_child(handle, node3b_h, "Elements");

	setdesc(handle, node3b_h, (char[]){0x00, 0x00, 0x10, 0x20});



	char *node3 = BOOTLDRSETTINGS;
	hive_node_h node3_h = hivex_node_add_child(handle, objs, node3);
	hive_node_h node3_elem_h = hivex_node_add_child(handle, node3_h, "Elements");

	setdesc(handle, node3_h, (char[]){0x03, 0x00, 0x20, 0x20});

	{
		const int size = 158;
		char bt14000006[158] = {0};
		get_utf16le(GLOBALSETTINGS, bt14000006, 76);
		// Jugaad for weird string format
		get_utf16le(HYPERVISORSETTINGS, bt14000006 + 78, 76);
		insertval(handle, node3_elem_h, "14000006", hive_t_REG_MULTI_SZ, size, bt14000006);
	}



	char *node4 = GLOBALSETTINGS; // Used in node1_elem_14000006_elem
	hive_node_h node4_h = hivex_node_add_child(handle, objs, node4);
	hive_node_h node4_elem_h = hivex_node_add_child(handle, node4_h, "Elements");

	setdesc(handle, node4_h, (char[]){0x00, 0x00, 0x10, 0x20});

	{
		const int size = 236;
		char bt14000006[236] = {0};
		get_utf16le(DBGSETTINGS, bt14000006, 76);
		// Turiksu for weird string format
		get_utf16le(EMSSETTINGS, bt14000006 + 78, 76);
		// LMAO yeh chal kyun raha hai 😂😂
		get_utf16le(BADMEMORY, bt14000006 + 78 + 76 + 2, 76);
		insertval(handle, node4_elem_h, "14000006", hive_t_REG_MULTI_SZ, size, bt14000006);
	}



	char *node5a = HYPERVISORSETTINGS;
	hive_node_h node5a_h = hivex_node_add_child(handle, objs, node5a);
	hive_node_h node5a_elem_h = hivex_node_add_child(handle, node5a_h, "Elements");

	setdesc(handle, node5a_h, (char[]){0x03, 0x00, 0x20, 0x20});

	{
		int size = 8;
		char bt250000f3[8] = {0};
		insertval(handle, node5a_elem_h, "250000f3", hive_t_REG_BINARY, size, bt250000f3);
	}
	{
		int size = 8;
		char bt250000f4[8] = {0x01};
		insertval(handle, node5a_elem_h, "250000f4", hive_t_REG_BINARY, size, bt250000f4);
	}
	{
		int size = 8;
		char bt250000f5[8] = {[1] = 0xC2, [2] = 0x01};
		insertval(handle, node5a_elem_h, "250000f5", hive_t_REG_BINARY, size, bt250000f5);
	}

	char *node5 = BOOTMGR;
	hive_node_h node5_h = hivex_node_add_child(handle, objs, node5);
	hive_node_h node5_elem_h = hivex_node_add_child(handle, node5_h, "Elements");

	setdesc(handle, node5_h, (char[]){0x02, 0x00, 0x10, 0x10});

	{
		const int size = 88;
		char* bt11000001 = esp_bytes;
		insertval(handle, node5_elem_h, "11000001", hive_t_REG_BINARY, size, bt11000001);
	}
	{
		const int size = 66;
		char bt12000002[66] = {0};
		get_utf16le("\\EFI\\Microsoft\\Boot\\bootmgfw.efi", bt12000002, size);
		insertval(handle, node5_elem_h, "12000002", hive_t_REG_SZ, size, bt12000002);
	}
	{
		const int size = 42;
		char bt12000004[42] = {0};
		get_utf16le("Windows Boot Manager", bt12000004, size);
		insertval(handle, node5_elem_h, "12000004", hive_t_REG_SZ, size, bt12000004);
	}
	{
		const int size = 12;
		char bt12000005[12] = {0};
		get_utf16le(locale, bt12000005, size);
		insertval(handle, node5_elem_h, "12000005", hive_t_REG_SZ, size, bt12000005);
	}
	{
		const int size = 80;
		char bt14000006[80] = {0};
		get_utf16le(GLOBALSETTINGS, bt14000006, size);
		insertval(handle, node5_elem_h, "14000006", hive_t_REG_MULTI_SZ, size, bt14000006);
	}
	{
		const int size = 8;
		char bt25000004[8] = {0x1E};
		insertval(handle, node5_elem_h, "25000004", hive_t_REG_BINARY, size, bt25000004);
	}


	// printf("%s\n", hivex_value_key(handle, descarr[0]));
	// size_t len = 0;
	// hive_type type;
	// hivex_value_type(handle, descarr[0], &type,&len);
	// printf("Len: %d, Type: %d", len, type);

	// printf("%s\n", hivex_node_name(handle, desc));

	// hive_node_h *children = hivex_node_children(handle, root);
	// int size = hivex_node_nr_children(handle, root);
	// for (int i = 0; i < size; i++)
	// {
	// 	char *name = hivex_node_name(handle, children[i]);
	// 	printf("%s\n", name);
	// }

	hivex_commit(handle, NULL, 0);
	hivex_close(handle);

	return 0;
}

