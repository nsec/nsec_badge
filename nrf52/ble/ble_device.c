//
//  ble_device.c
//  nsec16
//
//  Created by Marc-Etienne M.Léveillé on 2015-11-22.
//
//

#include "nsec_ble_internal.h"
#include <ble_conn_params.h>
/*
#include <ble_bas.h>
#include <ble_hci.h>*/
#include <ble_advdata.h>
#include <ble_dis.h>
#include <app_timer.h>
#include <peer_manager.h>
#include <app_scheduler.h>
#include <nrf_ble_gatt.h>

#include "../boards.h"
#include <nrf_gpio.h>
#include <nrf_delay.h>
#include "../led_effects.h"
#include "../logs.h"
#include "nrf_log.h"
#include "gap_configuration.h"
#include "nsec_ble.h"
#include "vendor_service.h"
#include "service_characteristic.h"


#define APP_BLE_OBSERVER_PRIO 3
#define PEER_ADDRESS_SIZE 6
#define MAX_VENDOR_SERVICE_COUNT 8
#define LONG_WRITE_MAX_LENGTH 200
NRF_BLE_GATT_DEF(m_gatt);


typedef struct{
    const char* device_name;
    ble_gap_adv_params_t advertising_parameters;
    uint16_t vendor_service_count;
    VendorService* vendor_services[MAX_VENDOR_SERVICE_COUNT];
} BleDevice;

static BleDevice* ble_device = NULL;

static uint8_t* buffer = NULL;

static nrf_sdh_ble_evt_handler_t _nsec_ble_event_handlers[NSEC_BLE_LIMIT_MAX_EVENT_HANDLER];
static nsec_ble_adv_uuid_provider _nsec_ble_adv_uuid_providers[NSEC_BLE_LIMIT_MAX_UUID_PROVIDER];
static uint8_t _nsec_ble_is_enabled = 0;
static nsec_ble_found_nsec_badge_callback _nsec_ble_scan_callback = NULL;

//static void nsec_ble_scan_start();
static void _nsec_ble_softdevice_init();
static void gatt_init();
//static void init_connection_parameters();
//static void add_device_information_service(char * manufacturer_name, char * model, char * serial_number,
//        char * hw_revision, char * fw_revision, char * sw_revision);
static void on_characteristic_write_command_event(const ble_gatts_evt_write_t * write_event);
static void on_characteristic_write_request_event(const ble_gatts_evt_write_t * write_event, uint16_t connection_handle);
static void on_characteristic_read_request_event(const ble_gatts_evt_read_t * read_event, uint16_t connection_handle);
static ServiceCharacteristic* get_characteristic_from_uuid(uint16_t uuid);
static void reply_to_client_request(uint8_t operation, uint16_t status_code, uint16_t connection_handle,
        const uint8_t* data_buffer, uint16_t data_length);
static void on_prepare_write_request(const ble_gatts_evt_write_t * write_event, uint16_t connection_handle);
static void on_execute_queued_write_requests(const ble_gatts_evt_write_t * write_event, uint16_t connection_handle);
static uint16_t get_queued_write_count();
static uint16_t get_characteristic_handle_for_queued_writes();
static ServiceCharacteristic* get_characteristic_from_handle(uint16_t handle);


ret_code_t create_ble_device(char* device_name){
    if(ble_device == NULL){
        _nsec_ble_softdevice_init();
        pm_init();
        nsec_ble_init();
        ble_device = malloc(sizeof(BleDevice));
        ble_device->device_name = device_name;
        ble_device->vendor_service_count = 0;
        for(int i = 0; i < MAX_VENDOR_SERVICE_COUNT; i++){
            ble_device->vendor_services[i] = NULL;
        }
        return NRF_SUCCESS;
    }
    return -1;
}

void configure_advertising(){
    if(ble_device == NULL)
        return;
    gatt_init();
    set_default_gap_parameters(ble_device->device_name, &(ble_device->advertising_parameters));
}


void start_advertising(){
    //init_connection_parameters();
    log_error_code("sd_ble_gap_tx_power_set", sd_ble_gap_tx_power_set(4));
    NRF_LOG_INFO("Starting advertising");
    if(ble_device == NULL)
                return;
    uint32_t error_code;
    error_code = sd_ble_gap_adv_start(&(ble_device->advertising_parameters), BLE_COMMON_CFG_VS_UUID);
    log_error_code("sd_ble_gap_adv_start", error_code);
}

static void ble_event_handler(ble_evt_t const * p_ble_evt, void * p_context){
    //pm_on_ble_evt(p_ble_evt);
    switch (p_ble_evt->header.evt_id){
        case BLE_GAP_EVT_CONNECTED: {
            //const uint8_t * addr = p_ble_evt->evt.gap_evt.params.connected.peer_addr.addr;
            NRF_LOG_INFO("Connected to a device"); //TODO print address of peer.
            }
            break;

        case BLE_GAP_EVT_DISCONNECTED:
            NRF_LOG_INFO("Disconnected. Reason: %d", p_ble_evt->evt.gap_evt.params.disconnected.reason);
            break; // TODO re-enter advertising mode?

        case BLE_GATTS_EVT_SYS_ATTR_MISSING: {
            const uint16_t conn = p_ble_evt->evt.gatts_evt.conn_handle;
            APP_ERROR_CHECK(sd_ble_gatts_sys_attr_set(conn, NULL, 0, 0));
            }
            break;

        case BLE_GAP_EVT_ADV_REPORT: {
            if(_nsec_ble_scan_callback == NULL) {
                break;
            }
            const ble_gap_evt_adv_report_t * rp = &p_ble_evt->evt.gap_evt.params.adv_report;
            int8_t i = 0;
            while((rp->dlen - i) >= 2) {
                const uint8_t len = rp->data[i++] - 1; // The type is included in the length
                const uint8_t type = rp->data[i++];
                const uint8_t * data = &rp->data[i];
                i += len;

                if(type == BLE_GAP_AD_TYPE_COMPLETE_LOCAL_NAME) {
                    if(len == 8 && data[0] == 'N' && data[1] == 'S' && data[2] == 'E' && data[3] == 'C') {
                        uint16_t other_id = 0;
                        if(sscanf((const char *) &data[4], "%04hx", &other_id) == 1) {
                            _nsec_ble_scan_callback(other_id, rp->peer_addr.addr, rp->rssi);
                        }
                    }
                }
            }
            }
            break;
        case BLE_GAP_EVT_SEC_PARAMS_REQUEST:
            NRF_LOG_INFO("Central is attempting pairing.");
            break;
        case BLE_GATTS_EVT_WRITE:
        {
            const ble_gatts_evt_write_t * p_evt_write = &p_ble_evt->evt.gatts_evt.params.write;
            on_characteristic_write_command_event(p_evt_write);
            break;
        }
        case BLE_GATTS_EVT_EXCHANGE_MTU_REQUEST:
            NRF_LOG_INFO("MTU exchange requested");
            break;
        case BLE_GATTS_EVT_RW_AUTHORIZE_REQUEST:
        {
            uint8_t type = p_ble_evt->evt.gatts_evt.params.authorize_request.type;
            if(type == BLE_GATTS_AUTHORIZE_TYPE_READ){
                const ble_gatts_evt_read_t * event = &p_ble_evt->evt.gatts_evt.params.authorize_request.request.read;
                on_characteristic_read_request_event(event, p_ble_evt->evt.gatts_evt.conn_handle);
            }
            else if(type == BLE_GATTS_AUTHORIZE_TYPE_WRITE){
                const ble_gatts_evt_write_t * event = &p_ble_evt->evt.gatts_evt.params.authorize_request.request.write;
                uint16_t connection_handle = p_ble_evt->evt.gatts_evt.conn_handle;
                switch(event->op){
                	case BLE_GATTS_OP_WRITE_REQ:
                		on_characteristic_write_request_event(event, connection_handle);
                		break;
                	case BLE_GATTS_OP_PREP_WRITE_REQ:
                		on_prepare_write_request(event, connection_handle);
                		break;
                	case BLE_GATTS_OP_EXEC_WRITE_REQ_NOW:
                		on_execute_queued_write_requests(event, connection_handle);
                		break;
                	default:
                		APP_ERROR_CHECK(event->op);
                }
            }
            break;
        }
        case BLE_EVT_USER_MEM_REQUEST:
        {
            buffer = malloc(LONG_WRITE_MAX_LENGTH);
            ble_user_mem_block_t memory_block;
            memory_block.p_mem = buffer;
            memory_block.len = LONG_WRITE_MAX_LENGTH;
            APP_ERROR_CHECK(sd_ble_user_mem_reply(p_ble_evt->evt.common_evt.conn_handle, &memory_block));
        }
            break;
        case BLE_EVT_USER_MEM_RELEASE:
            free(p_ble_evt->evt.common_evt.params.user_mem_release.mem_block.p_mem);
            buffer = NULL;
            break;
        default:
            APP_ERROR_CHECK(p_ble_evt->header.evt_id);
    }
}

uint32_t add_vendor_service(VendorService* service){
    if(ble_device->vendor_service_count >= MAX_VENDOR_SERVICE_COUNT)
        return 1;
    create_uuid_for_vendor_service(&service->uuid, ble_device->vendor_service_count);
    uint32_t error_code = sd_ble_gatts_service_add(BLE_GATTS_SRVC_TYPE_PRIMARY, &service->uuid, &service->handle);
    APP_ERROR_CHECK(error_code);
    ble_device->vendor_services[ble_device->vendor_service_count] = service;
    ble_device->vendor_service_count++;
    return 0;
}

/*
 * At least one BLE service must be advertised for the advertising to work. This dummy service is a place holder
 * to make it work until a real service is added to assume this role.
 */
void config_dummy_service(VendorService* dummy_service, ServiceCharacteristic* characteristic){
    add_vendor_service(dummy_service);
    add_characteristic_to_vendor_service(dummy_service, characteristic, 30, AUTO_READ, REQUEST_WRITE);
    set_default_advertised_service(dummy_service);
}

static void gatt_init(){
    ret_code_t err_code = nrf_ble_gatt_init(&m_gatt, NULL);
    log_error_code("nrf_ble_gatt_init",err_code);
}

/*static void _nsec_pm_evt_handler(pm_evt_t const * event) {

}*/

/*static void on_connection_params_event(ble_conn_params_evt_t * p_evt){
    if (p_evt->evt_type == BLE_CONN_PARAMS_EVT_FAILED){
        NRF_LOG_ERROR("BLE connection params event failed.");
    }
}*/

/*static void connection_params_error_handler(uint32_t nrf_error){
    log_error_code("connection params error handler", nrf_error);
}*/

static void on_characteristic_write_command_event(const ble_gatts_evt_write_t * write_event){
    if(write_event->op == BLE_GATTS_OP_EXEC_WRITE_REQ_NOW)
        return;
    ServiceCharacteristic* characteristic = get_characteristic_from_uuid(write_event->uuid.uuid);
    if(characteristic != NULL && characteristic->on_write_command != NULL){
        CharacteristicWriteEvent event = {
            .write_offset = write_event->offset,
            .data_length = write_event->len,
            .data_buffer = write_event->data
        };
        characteristic->on_write_command(&event);
    }
}

static void on_characteristic_write_request_event(const ble_gatts_evt_write_t * write_event, uint16_t connection_handle){
    ServiceCharacteristic* characteristic = get_characteristic_from_uuid(write_event->uuid.uuid);
    if(characteristic != NULL && characteristic->on_write_request != NULL){
        CharacteristicWriteEvent event = {
            .write_offset = write_event->offset,
            .data_length = write_event->len,
            .data_buffer = write_event->data
        };
        uint16_t status_code = BLE_GATT_STATUS_SUCCESS;
        uint8_t* data_buffer = status_code == BLE_GATT_STATUS_SUCCESS ? event.data_buffer: NULL;
        reply_to_client_request(BLE_GATTS_AUTHORIZE_TYPE_WRITE, status_code, connection_handle, data_buffer,
                write_event->len);
        status_code = characteristic->on_write_request(&event);
    }
    else{
        reply_to_client_request(BLE_GATTS_AUTHORIZE_TYPE_WRITE, BLE_GATT_STATUS_ATTERR_WRITE_NOT_PERMITTED,
                        connection_handle, NULL, 0);
    }
}

static void on_characteristic_read_request_event(const ble_gatts_evt_read_t * read_event, uint16_t connection_handle){
    ServiceCharacteristic* characteristic = get_characteristic_from_uuid(read_event->uuid.uuid);
    if(characteristic != NULL && characteristic->on_read_request != NULL){
        CharacteristicReadEvent event = {
            .read_offset = read_event->offset,
        };
        uint16_t status_code = characteristic->on_read_request(&event);
        reply_to_client_request(BLE_GATTS_AUTHORIZE_TYPE_READ, status_code, connection_handle, NULL, characteristic->value_length);
    }
    else{
        reply_to_client_request(BLE_GATTS_AUTHORIZE_TYPE_READ, BLE_GATT_STATUS_ATTERR_READ_NOT_PERMITTED, connection_handle, NULL, 0);
    }
}

static void on_prepare_write_request(const ble_gatts_evt_write_t * write_event, uint16_t connection_handle){
	if(get_characteristic_handle_for_queued_writes() == write_event->handle){
		reply_to_client_request(BLE_GATTS_AUTHORIZE_TYPE_WRITE, BLE_GATT_STATUS_SUCCESS, connection_handle, NULL, 0);
	}
	else{
		/* Queuing multiple writes to different characteristics should be allowed, but it would be difficult to handle properly and simply. */
		reply_to_client_request(BLE_GATTS_AUTHORIZE_TYPE_WRITE, BLE_GATT_STATUS_ATTERR_WRITE_NOT_PERMITTED, connection_handle, write_event->data, write_event->len);
	}
}

static void on_execute_queued_write_requests(const ble_gatts_evt_write_t * write_event, uint16_t connection_handle){
	APP_ERROR_CHECK(buffer == NULL);
	uint16_t status_code = BLE_GATT_STATUS_SUCCESS;
	uint16_t index = 0;
	uint16_t handle = *((uint16_t*)buffer);
	while(handle != BLE_GATT_HANDLE_INVALID){
		uint16_t write_offset = *((uint16_t*)(buffer + index + 2));
		uint16_t write_length = *((uint16_t*)(buffer + index + 4));
		uint8_t* value_buffer = buffer + index + 6;
		ServiceCharacteristic* characteristic = get_characteristic_from_handle(handle);
		if(characteristic != NULL && characteristic->on_write_request != NULL){
			CharacteristicWriteEvent event = {
				.write_offset = write_offset,
				.data_length = write_length,
				.data_buffer =  value_buffer
			};
			status_code = characteristic->on_write_request(&event);
			if(status_code != BLE_GATT_STATUS_SUCCESS){
				break;
			}
		}
		index += sizeof(handle) + sizeof(write_offset) + sizeof(write_length) + write_length;
		handle = *((uint16_t*)(buffer + index));
	}
	if(status_code == BLE_GATT_STATUS_SUCCESS){
		reply_to_client_request(BLE_GATTS_AUTHORIZE_TYPE_WRITE, status_code, connection_handle, NULL, 0);
	}
	else{
		reply_to_client_request(BLE_GATTS_AUTHORIZE_TYPE_WRITE, BLE_GATT_STATUS_ATTERR_WRITE_NOT_PERMITTED,
				connection_handle, NULL, 0);
	}
}

static void reply_to_client_request(uint8_t operation, uint16_t status_code, uint16_t connection_handle,
        const uint8_t* data_buffer, uint16_t data_length){
    ble_gatts_rw_authorize_reply_params_t reply;
    reply.type = operation;
    reply.params.read.gatt_status = status_code;
    reply.params.read.len = data_length;
    reply.params.read.offset = 0;
    reply.params.read.update = data_buffer == NULL ? false: true;
    reply.params.read.p_data = data_buffer;
    APP_ERROR_CHECK(sd_ble_gatts_rw_authorize_reply(connection_handle, &reply));
}

static ServiceCharacteristic* get_characteristic_from_uuid(uint16_t uuid){
    for(int i = 0; i < ble_device->vendor_service_count; i++){
        VendorService* service = ble_device->vendor_services[i];
        if(service->uuid.uuid == (uuid & UUID_SERVICE_PART_MASK))
            return get_characteristic(service, uuid);
    }
    return NULL;
}

static ServiceCharacteristic* get_characteristic_from_handle(uint16_t handle){
    for(int i = 0; i < ble_device->vendor_service_count; i++){
        VendorService* service = ble_device->vendor_services[i];
        for(int j = 0; j < service->characteristic_count; j++){
        	ServiceCharacteristic* characteristic = service->characteristics[j];
        	if(characteristic->handle == handle){
        		return characteristic;
        	}
        }
    }
    return NULL;
}

static uint16_t get_queued_write_count(){
	if(buffer == NULL){
		return 0;
	}
	uint16_t count = 0;
	uint16_t handle = *((uint16_t*)buffer);
	uint16_t index = 0;
	while(handle != BLE_GATT_HANDLE_INVALID){
		count++;
		uint16_t current_write_length = *((uint16_t*)(buffer + index + 4));
		index += 6 + current_write_length;
		handle = *((uint16_t*)(buffer + index));
	}
	return count;
}

static uint16_t get_characteristic_handle_for_queued_writes(){
	if(buffer == NULL){
		return BLE_GATT_HANDLE_INVALID;
	}
	uint16_t handle = *((uint16_t*)buffer);
	return handle;
}

/*static void init_connection_parameters(){
    // The BLE stack seems to work without this function, but I leave it here just in case it might be needed in the future.

    ret_code_t             err_code;
    ble_conn_params_init_t cp_init;

    memset(&cp_init, 0, sizeof(cp_init));

    cp_init.p_conn_params                  = NULL;
    cp_init.first_conn_params_update_delay = APP_TIMER_TICKS(20000);
    cp_init.next_conn_params_update_delay  = APP_TIMER_TICKS(5000);
    cp_init.max_conn_params_update_count   = 3;
    cp_init.start_on_notify_cccd_handle    = BLE_GATT_HANDLE_INVALID;
    cp_init.disconnect_on_fail             = false;
    cp_init.evt_handler                    = on_connection_params_event;
    cp_init.error_handler                  = connection_params_error_handler;

    err_code = ble_conn_params_init(&cp_init);
    log_error_code("ble_conn_params_init", err_code);
}*/

/*static void add_device_information_service(char * manufacturer_name, char * model, char * serial_number,
        char * hw_revision, char * fw_revision, char * sw_revision){
    ble_dis_init_t device_information;
    bzero(&device_information, sizeof(device_information));

    if(manufacturer_name) {
        ble_srv_ascii_to_utf8(&device_information.manufact_name_str,
                              manufacturer_name);
    }
    if(model) {
        ble_srv_ascii_to_utf8(&device_information.model_num_str,
                              model);
    }
    if(serial_number) {
        ble_srv_ascii_to_utf8(&device_information.serial_num_str,
                              serial_number);
    }
    if(hw_revision) {
        ble_srv_ascii_to_utf8(&device_information.hw_rev_str,
                              hw_revision);
    }
    if(fw_revision) {
        ble_srv_ascii_to_utf8(&device_information.fw_rev_str,
                              fw_revision);
    }
    if(sw_revision) {
        ble_srv_ascii_to_utf8(&device_information.sw_rev_str,
                              sw_revision);
    }
    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&device_information.dis_attr_md.read_perm);
    log_error_code("ble_dis_init", ble_dis_init(&device_information));
}*/

static void _nsec_ble_softdevice_init() {
    uint32_t ram_start = 0;
    nrf_sdh_ble_default_cfg_set(BLE_COMMON_CFG_VS_UUID, &ram_start);

#if DEBUG_PRINT_RAM_USAGE
    uint32_t ram_start_copy = 0;
    nrf_sdh_ble_enable(&ram_start_copy);
    //TO DO
#else
    APP_ERROR_CHECK(nrf_sdh_ble_enable(&ram_start));
#endif
    // Register a handler for BLE events.
    NRF_SDH_BLE_OBSERVER(m_ble_observer, APP_BLE_OBSERVER_PRIO, ble_event_handler, NULL);
}
/*
static void nsec_ble_disable_task(void * context, uint16_t size) {
    sd_ble_gap_scan_stop();
    sd_ble_gap_adv_stop();
}

static void nsec_ble_enable_task(void * context, uint16_t size) {
    _nsec_ble_advertising_start();
    nsec_ble_scan_start();
}

uint8_t nsec_ble_toggle(void) {
    if(_nsec_ble_is_enabled) {
        app_sched_event_put(NULL, 0, nsec_ble_disable_task);
        _nsec_ble_is_enabled = 0;
    }
    else {
        app_sched_event_put(NULL, 0, nsec_ble_enable_task);
        _nsec_ble_is_enabled = 1;
    }
    return _nsec_ble_is_enabled;
}
*/
uint8_t nsec_ble_toggle(void) { return 0; } // FIXME

void nsec_ble_register_evt_handler(nrf_sdh_ble_evt_handler_t handler) {
    for(int i = 0; i < NSEC_BLE_LIMIT_MAX_EVENT_HANDLER; i++) {
        if(_nsec_ble_event_handlers[i] == NULL) {
            _nsec_ble_event_handlers[i] = handler;
            break;
        }
    }
}
/*
void nsec_ble_set_scan_callback(nsec_ble_found_nsec_badge_callback callback) {
    _nsec_ble_scan_callback = callback;
}
*/
void nsec_ble_register_adv_uuid_provider(nsec_ble_adv_uuid_provider provider) {
    for(int i = 0; i < NSEC_BLE_LIMIT_MAX_UUID_PROVIDER; i++) {
        if(_nsec_ble_adv_uuid_providers[i] == NULL) {
            _nsec_ble_adv_uuid_providers[i] = provider;
            break;
        }
    }
    sd_ble_gap_adv_stop();
}
/*
static void nsec_ble_scan_start(void) {
    ble_gap_scan_params_t scan_params;
    scan_params.active = 0;
    scan_params.adv_dir_report = 0;
    scan_params.use_whitelist = 0;
    scan_params.timeout = 0;
    scan_params.window = MSEC_TO_UNITS(40, UNIT_0_625_MS);
    scan_params.interval = MSEC_TO_UNITS(240, UNIT_0_625_MS);

    log_error_code("sd_ble_gap_scan_start", sd_ble_gap_scan_start(&scan_params));
}
*/
int nsec_ble_init() {
    bzero(_nsec_ble_event_handlers, sizeof(_nsec_ble_event_handlers));
    bzero(_nsec_ble_adv_uuid_providers, sizeof(_nsec_ble_adv_uuid_providers));

    //nsec_ble_scan_start();
    _nsec_ble_is_enabled = 1;

    return NRF_SUCCESS;
}
