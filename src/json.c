#include <efiboot.h>
#include <jansson.h>

#include "parse_loader_data.h"
#include "efibootmgr.h"
#include "error.h"
#include "json.h"

static void
json_fill_bootnext(json_t *root)
{
	char s[5] = {};
	json_t *value;
	int num;

	num = read_u16("BootNext");
	cond_warning(opts.verbose >= 2 && num < 0,
			"Could not read variable 'BootNext'");
	if (num >= 0) {
		snprintf(s, sizeof(s), "%04X", num);
		value = json_string(s);
		json_object_set_new(root, "BootNext", value);
	}
}

static void
json_fill_bootcurrent(json_t *root)
{
	char s[5] = {};
	json_t *value;
	int num;

	num = read_u16("BootCurrent");
	cond_warning(opts.verbose >= 2 && num < 0,
			"Could not read variable 'BootCurrent'");
	if (num >= 0) {
		snprintf(s, sizeof(s), "%04X", num);
		value = json_string(s);
		json_object_set_new(root, "BootCurrent", value);
	}
}

static void
json_fill_timeout(json_t *root)
{
	json_t *value;
	int num;

	num = read_u16("Timeout");
	cond_warning(opts.verbose >= 2 && num < 0,
			"Could not read variable 'Timeout'");
	if (num >= 0) {
		value = json_integer(num);
		json_object_set_new(root, "Timeout", value);
	}
}

static json_t *
bootorder_json_array(uint16_t *order, int length)
{
	json_t *value, *array;
	char s[5] = {};
	int i;

	array = json_array();
	if (!array)
		return NULL;

	for (i = 0; i < length; i++) {
		snprintf(s, sizeof(s), "%04X", order[i]);
		value = json_string(s);
		json_array_append_new(array, value);
	}

	return array;
}

static void
json_fill_order(json_t *root, const char *name)
{
	var_entry_t *order = NULL;
	uint16_t *data;
	json_t *array;
	int rc;

	rc = read_order(name, &order);
	cond_warning(opts.verbose >= 2 && rc < 0,
		  "Could not read variable '%s'", name);

	if (rc < 0) {
		if (errno == ENOENT) {
			if (!strcmp(name, "BootOrder"))
				printf("No BootOrder is set; firmware will attempt recovery\n");
			else
				printf("No %s is set\n", name);
		} else
			perror("json_fill_order()");
		return;
	}

	data = (uint16_t *)order->data;
	if (order->data_size) {
		array = bootorder_json_array(data,
					order->data_size / sizeof(uint16_t));
		if (array != NULL)
			json_object_set_new(root, name, array);
		free(order->data);
	}
	free(order);
}

static void
json_fill_vars(json_t *root, const char *prefix, list_t *entry_list)
{
	const unsigned char *description;
	json_t *boot_json, *vars_json;
	efi_load_option *load_option;
	char name[16] = {'\0'};
	var_entry_t *boot;
	list_t *pos;
	int active;

	vars_json = json_array();
	if (!vars_json)
		return;

	list_for_each(pos, entry_list) {
		boot_json = json_object();
		boot = list_entry(pos, var_entry_t, list);
		load_option = (efi_load_option *)boot->data;
		description = efi_loadopt_desc(load_option, boot->data_size);
		if (boot->name)
			json_object_set_new(boot_json, "name", json_string(boot->name));
		else {
			snprintf(name, sizeof(name), "%s%04X", prefix, boot->num);
			json_object_set_new(boot_json, "name", json_string(boot->name));
		}

		active = efi_loadopt_attrs(load_option) & LOAD_OPTION_ACTIVE ? 1 : 0;
		json_object_set_new(boot_json, "active", json_boolean(active));
		json_object_set_new(boot_json, "description",
				json_string((char *)description));
		json_array_append_new(vars_json, boot_json);
	}

	json_object_set_new(root, "vars", vars_json);
}

void
__print_json(list_t *entry_list, ebm_mode mode, char **prefices, char **order_name)
{
	json_t *root = json_object();
	char *json_str = NULL;

	switch (mode) {
		case boot:
			json_fill_bootnext(root);
			json_fill_bootcurrent(root);
			json_fill_timeout(root);
			json_fill_order(root, order_name[mode]);
			json_fill_vars(root, prefices[mode], entry_list);
			break;
		case driver:
		case sysprep:
			json_fill_order(root, order_name[mode]);
			json_fill_vars(root, prefices[mode], entry_list);
			break;
	}
	json_str = json_dumps(root, JSON_COMPACT);
	printf("%s\n", json_str);
	free(json_str);
	json_decref(root);
}
