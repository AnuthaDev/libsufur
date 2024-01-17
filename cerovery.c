#include <stdio.h>
#include <hivex.h>

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

static char locale[] = "en-us";


static char disk_bits[16] = {0xB2, 0x13, 0x7B, 0x4D, 0x91, 0xEA, 0x4A, 0xB3, 0x81, 0x25, 0x1B, 0x98, 0x3C, 0x97, 0xF7, 0x63};
static char esp_part_bits[16] = {0x15, 0xC1, 0xEE, 0x27, 0x0B, 0x86, 0x44, 0xEB, 0x9D, 0xBA, 0x70, 0xE0, 0x51, 0xC7, 0xAA, 0x11};
static char boot_part_bits[16] = {0x68, 0x52, 0xC7, 0xA8, 0xF5, 0x56, 0x4B, 0xC8, 0x8C, 0x75, 0xB9, 0x64, 0x7B, 0x69, 0x12, 0x06};



static char boot_partition_bytes[88] = {[16] = 0x06, [24] = 0x48};
static char esp_bytes[88] = {[16] = 0x06, [24] = 0x48};


// https://devblogs.microsoft.com/oldnewthing/20091008-00/?p=16443

void insertval(hive_h *handle, hive_node_h node_elem_h, char *nodename, hive_type type, int size, char data[])
{
	hive_node_h node_elem_val_h = hivex_node_add_child(handle, node_elem_h, nodename);

	hive_set_value node_elem_val_elem = {
			.key = "Element",
			.t = type,
			.len = size,
			.value = data};
	hivex_node_set_value(handle, node_elem_val_h, &node_elem_val_elem, 0);
}

void setdesc(hive_h *handle, hive_node_h node_h, char bytes[])
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

void get_utf16le(char *src, char dest[], int size)
{
	for (int i = 0, j = 0; i < size && src[j] != '\0'; i += 2, j++)
	{
		dest[i] = src[j];
	}
}


void setbytearray(char dest[88], char disk_bits[16], char part_bits[16]){
	for(int i = 0; i<16;i++){
		dest[i+32] = part_bits[i];
		dest[i+56] = disk_bits[i];
	}
}


int main()
{
	setbytearray(boot_partition_bytes, disk_bits, boot_part_bits);
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
