#include "provisioning/provisioning_manager.hpp"

#include "cJSON.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "nvs_flash.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <string_view>

namespace tank_monitor::provisioning {
namespace {

constexpr char kLogTag[] = "provisioning";
constexpr char kPortalIp[] = "192.168.4.1";
constexpr std::uint16_t kDnsPort = 53;
constexpr std::size_t kDnsPacketMaximum = 512;

constexpr char kPortalHtml[] = R"html(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>LoRa Tank Monitor</title><style>
:root{color-scheme:light;--ink:#17211c;--muted:#647068;--line:#cad4cd;--paper:#f5f8f5;--accent:#006a4e;--danger:#a12a2a}
*{box-sizing:border-box}body{margin:0;background:linear-gradient(135deg,#e6eee8,#fbfcfb 55%,#e7edf2);color:var(--ink);font:15px/1.4 Georgia,serif}
main{width:min(820px,calc(100% - 24px));margin:20px auto;background:#fff;border:1px solid var(--line);border-radius:8px;box-shadow:0 14px 40px #1835271c;overflow:hidden}
header{padding:22px 26px;border-bottom:4px solid var(--accent);display:flex;justify-content:space-between;gap:16px;align-items:end}h1{margin:0;font-size:28px;letter-spacing:0}header span{color:var(--accent);font-weight:bold}
section,details{padding:20px 26px;border-bottom:1px solid var(--line)}h2,summary{font-size:18px;font-weight:bold;margin:0 0 15px}summary{cursor:pointer;margin:0}details[open] summary{margin-bottom:16px}.grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:13px 18px}.wide{grid-column:1/-1}
label{display:grid;gap:5px;color:var(--muted)}input,select{width:100%;border:1px solid #aebbb2;border-radius:5px;padding:9px 10px;background:#fff;color:var(--ink);font:inherit}input[type=checkbox]{width:auto}.check{display:flex;align-items:center;gap:8px;color:var(--ink)}
.role{display:flex;gap:18px}.role label{display:flex;grid-template-columns:auto 1fr;align-items:center;color:var(--ink)}button{border:0;border-radius:5px;background:var(--accent);color:#fff;padding:10px 15px;font:inherit;font-weight:bold;cursor:pointer}button.secondary{background:#44534b}button.danger{background:var(--danger)}button:disabled{opacity:.55;cursor:not-allowed}.actions{display:flex;gap:10px;align-items:center;flex-wrap:wrap}.message{font-weight:bold;color:var(--accent)}.message.error{color:var(--danger)}small{color:var(--muted)}
.alarm{padding:14px 0;border-top:1px dashed var(--line)}.alarm:first-of-type{border-top:0}.pins{grid-template-columns:repeat(4,minmax(0,1fr))}@media(max-width:620px){header{display:block}.grid,.pins{grid-template-columns:1fr}.wide{grid-column:auto}section,details{padding:18px}main{margin:12px auto}}
</style></head><body><main><header><div><h1>LoRa Tank Monitor</h1><small>Configuration is staged until saved</small></div><span id="ap">Provisioning</span></header>
<form id="config"><section><h2>Device</h2><div class="wide role"><label><input type="radio" name="role" value="tx" checked> Tank</label><label><input type="radio" name="role" value="rx"> Gateway</label></div></section>
<section><h2>Wi-Fi</h2><div class="grid"><label class="wide">Network<select name="wifi_ssid" id="wifi"><option value="">Select a network</option></select></label><label>Password<input name="wifi_password" maxlength="64" autocomplete="new-password" placeholder="Unchanged when blank"></label><div><button type="button" class="secondary" id="scan">Scan networks</button></div></div></section>
<details open><summary>LoRa Radio</summary><div class="grid"><label>Chip<select name="radio_chip"><option value="sx1278">SX1278</option><option value="sx1276">SX1276</option></select></label><label>Frequency (Hz)<input name="frequency_hz" type="number" value="433000000" required></label><label>Bandwidth (Hz)<select name="bandwidth_hz"><option>62500</option><option selected>125000</option><option>250000</option></select></label><label>Spreading factor<input name="spreading_factor" type="number" min="6" max="12" value="9" required></label><label>Coding-rate denominator<input name="coding_rate" type="number" min="5" max="8" value="5" required></label><label>Sync word<input name="sync_word" type="number" min="0" max="255" value="18" required></label><label>Preamble symbols<input name="preamble" type="number" value="8" required></label><label>TX power (dBm)<input name="tx_power" type="number" value="14" required></label><label>RX timeout (ms)<input name="rx_timeout_ms" type="number" value="2000" required></label><label>TX timeout (ms)<input name="tx_timeout_ms" type="number" value="5000" required></label></div></details>
<details open data-role="tx"><summary>Tank details</summary><div class="grid"><label>Sensor to bottom (cm)<input name="sensor_bottom_cm" type="number" step="any" value="220" required></label><label>Sensor to max surface (cm)<input name="sensor_surface_cm" type="number" step="any" value="40" required></label><label>Full capacity (units)<input name="tank_capacity_litres" type="number" step="any" value="1000" required></label><label>Display unit<select name="volume_unit"><option value="litres">Litres</option><option value="us_gallons">US gallons</option><option value="imperial_gallons">Imperial gallons</option></select></label><label>Out-of-range<select name="range_policy"><option value="reject">Reject</option><option value="clamp">Clamp</option><option value="report" selected>Report (clamp %)</option></select></label><label>Measurement interval (minutes)<input name="measurement_interval_minutes" type="number" min="1" max="1440" value="30" required></label><small class="wide">Bottom is the empty water surface. Max surface is the highest expected water surface, measured from the sensor.</small></div></details>
<details><summary>Alarms</summary><div id="alarms"></div></details>
<details><summary>Blynk</summary><div class="grid"><label class="check"><input type="checkbox" name="blynk_enabled" value="1"> Enabled</label><label>Host<input name="blynk_host" maxlength="64" value="blynk.cloud"></label><label>Port<input name="blynk_port" type="number" value="443" required></label><label>Template ID<input name="blynk_template" maxlength="64"></label><label>Device name<input name="blynk_device" maxlength="64"></label><label>Auth token<input name="blynk_token" type="password" maxlength="96" autocomplete="new-password" placeholder="Unchanged when blank"></label><label>Publish interval (s)<input name="blynk_interval_s" type="number" value="60" required></label><div class="wide grid pins" id="pins"></div></div></details>
<section><h2>Pairing</h2><div class="actions"><span id="pairing" class="message">Unpaired</span><button type="button" class="danger" id="clear-pairing" disabled>Clear pairing</button></div></section>
<section><div class="actions"><button type="submit">Validate staged configuration</button><button type="button" id="save" disabled>Save and reboot</button><button type="button" class="danger" id="reset">Factory reset</button><span id="message" class="message"></span></div></section></form></main>
<script>
let session='';const message=document.getElementById('message');
document.getElementById('alarms').closest('details').hidden=true;
const blynkMetrics=['Disabled','Distance raw (cm)','Total water (cm)','Tank percentage full','Water (litres)','Battery (V)','Battery percentage','Metadata','TX health','RX health'];
document.getElementById('pins').innerHTML=Array.from({length:12},(_,i)=>`<label>V${i}<select name="blynk_slot_${i}">${blynkMetrics.map((name,metric)=>`<option value="${metric-1}">${name}</option>`).join('')}</select></label>`).join('');
function updateRole(){const role=document.querySelector('input[name="role"]:checked').value;document.querySelectorAll('[data-role]').forEach(node=>{node.hidden=node.getAttribute('data-role')!==role});document.querySelectorAll('section,details').forEach(node=>{const title=(node.querySelector('h2,summary')||{}).textContent||'';if(['Wi-Fi','Blynk','Pairing'].includes(title))node.hidden=role!=='rx'})}
['display_enabled','display_timeout_s','long_press_ms','hostname','wifi_retry_min_ms','wifi_retry_max_ms'].forEach(name=>{const field=document.querySelector(`[name="${name}"]`);const label=field?field.closest('label'):null;if(label)label.setAttribute('hidden','')});
const sensorSection=[...document.querySelectorAll('details')].find(node=>(node.querySelector('summary')||{}).textContent==='Sensor and timing');if(sensorSection)sensorSection.hidden=true;
const batterySection=[...document.querySelectorAll('details')].find(node=>(node.querySelector('summary')||{}).textContent==='Battery');if(batterySection)batterySection.hidden=true;
function applyValues(values){Object.entries(values).forEach(([name,value])=>{const fields=document.querySelectorAll(`[name="${name}"]`);fields.forEach(field=>{if(field.type==='radio')field.checked=field.value===String(value);else if(field.type==='checkbox')field.checked=Boolean(value);else{if(field.tagName==='SELECT'&&![...field.options].some(o=>o.value===String(value))&&value)field.add(new Option(String(value),String(value)));field.value=String(value)}})})}
async function init(){const r=await fetch('/api/status');if(!r.ok)throw new Error('status');const s=await r.json();session=s.session;document.getElementById('ap').textContent=s.ap;const current=await fetch('/api/config/current');if(!current.ok)throw new Error('config');const c=await current.json();if(c.configured||c.staged)applyValues(c.values);const paired=Boolean(c.paired);document.getElementById('pairing').textContent=paired?`Paired TX: ${c.paired_node_id}`:'Unpaired';document.getElementById('clear-pairing').disabled=!paired;updateRole();}
async function loadNetworks(){message.textContent='Scanning...';message.classList.remove('error');await fetch('/api/wifi/scan',{method:'POST',headers:{'X-LTM-Session':session}});for(let i=0;i<20;i++){await new Promise(r=>setTimeout(r,500));const result=await fetch('/api/wifi/networks');const data=await result.json();if(!data.scanning){const select=document.getElementById('wifi');select.replaceChildren(new Option('Select a network',''));data.networks.forEach(n=>select.add(new Option(`${n.ssid} (${n.rssi} dBm)`,n.ssid)));message.textContent=`${data.networks.length} networks found`;return;}}message.textContent='Scan timed out';message.classList.add('error');}
document.getElementById('scan').onclick=loadNetworks;
document.querySelectorAll('input[name="role"]').forEach(field=>field.onchange=updateRole);
document.getElementById('config').addEventListener('invalid',e=>{const details=e.target.closest('details');if(details)details.open=true;const label=e.target.closest('label');message.textContent='Check '+(label?label.textContent.trim():e.target.name);message.classList.add('error')},true);
document.getElementById('config').onsubmit=async e=>{e.preventDefault();message.textContent='Validating...';message.classList.remove('error');try{const body=new URLSearchParams(new FormData(e.target));const r=await fetch('/api/config/stage',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded','X-LTM-Session':session},body:body});const data=await r.json();document.getElementById('save').disabled=!data.valid;const reason=data.validation_error!==undefined?data.validation_error:data.error;message.textContent=data.valid?'Configuration is valid and staged':'Validation failed ('+reason+')';message.classList.toggle('error',!data.valid)}catch(error){document.getElementById('save').disabled=true;message.textContent='Validation request failed';message.classList.add('error')}};
document.getElementById('save').onclick=async()=>{message.textContent='Saving...';const r=await fetch('/api/config/save',{method:'POST',headers:{'X-LTM-Session':session}});const data=await r.json();message.textContent=data.saved?'Saved. Device is rebooting...':'Save failed';message.classList.toggle('error',!data.saved);};
document.getElementById('clear-pairing').onclick=async()=>{if(!confirm('Clear the paired transmitter and reboot?'))return;message.textContent='Clearing pairing...';const r=await fetch('/api/pairing/clear',{method:'POST',headers:{'X-LTM-Session':session,'X-LTM-Confirm':'CLEAR'}});const data=await r.json();message.textContent=data.cleared?'Pairing cleared. Device is rebooting...':'Clear pairing failed';message.classList.toggle('error',!data.cleared);};
document.getElementById('reset').onclick=async()=>{if(!confirm('Erase all saved configuration and reboot?'))return;message.textContent='Resetting...';const r=await fetch('/api/factory-reset',{method:'POST',headers:{'X-LTM-Session':session,'X-LTM-Confirm':'ERASE'}});const data=await r.json();message.textContent=data.reset?'Configuration erased. Rebooting...':'Factory reset failed';message.classList.toggle('error',!data.reset);};
init().catch(()=>{message.textContent='Status unavailable';message.classList.add('error')});
</script></body></html>)html";

httpd_handle_t http_server = nullptr;
TaskHandle_t dns_task_handle = nullptr;
std::atomic<bool> dns_running{false};
ProvisioningManager* active_manager = nullptr;

esp_err_t json_error(httpd_req_t* request, const char* status, const char* body);

struct NetworkResult {
    std::array<char, 33> ssid{};
    std::int8_t rssi{0};
    wifi_auth_mode_t authentication{WIFI_AUTH_OPEN};
};

constexpr std::size_t kMaximumNetworks = 16;
std::array<NetworkResult, kMaximumNetworks> networks{};
std::size_t network_count = 0;
std::atomic<bool> scan_in_progress{false};
portMUX_TYPE network_lock = portMUX_INITIALIZER_UNLOCKED;

void wifi_event_handler(void*, esp_event_base_t, std::int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        const auto* event = static_cast<const wifi_event_ap_staconnected_t*>(event_data);
        ESP_LOGI(kLogTag, "Portal client associated: %02x:%02x:%02x:%02x:%02x:%02x",
                 event->mac[0], event->mac[1], event->mac[2], event->mac[3], event->mac[4], event->mac[5]);
        return;
    }
    if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        const auto* event = static_cast<const wifi_event_ap_stadisconnected_t*>(event_data);
        ESP_LOGI(kLogTag, "Portal client disconnected: %02x:%02x:%02x:%02x:%02x:%02x",
                 event->mac[0], event->mac[1], event->mac[2], event->mac[3], event->mac[4], event->mac[5]);
        return;
    }
    if (event_id != WIFI_EVENT_SCAN_DONE) {
        return;
    }
    // Static: sys_evt task stack (2304 B) cannot hold ~1.3 KB of AP records.
    static std::array<wifi_ap_record_t, kMaximumNetworks> records{};
    std::uint16_t count = records.size();
    if (esp_wifi_scan_get_ap_records(&count, records.data()) != ESP_OK) {
        count = 0;
        static_cast<void>(esp_wifi_clear_ap_list());
    }
    portENTER_CRITICAL(&network_lock);
    network_count = count;
    for (std::size_t index = 0; index < count; ++index) {
        networks[index] = {};
        std::memcpy(networks[index].ssid.data(), records[index].ssid,
                    std::min(networks[index].ssid.size() - 1, sizeof(records[index].ssid)));
        networks[index].rssi = records[index].rssi;
        networks[index].authentication = records[index].authmode;
    }
    portEXIT_CRITICAL(&network_lock);
    scan_in_progress.store(false);
}

esp_err_t set_security_headers(httpd_req_t* request)
{
    esp_err_t error = httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    if (error == ESP_OK) {
        error = httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    }
    if (error == ESP_OK) {
        error = httpd_resp_set_hdr(request, "Content-Security-Policy",
                                   "default-src 'none'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; connect-src 'self'; form-action 'self'");
    }
    return error;
}

esp_err_t root_handler(httpd_req_t* request)
{
    ESP_LOGI(kLogTag, "Portal page requested");
    set_security_headers(request);
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    return httpd_resp_send(request, kPortalHtml, HTTPD_RESP_USE_STRLEN);
}

esp_err_t status_handler(httpd_req_t* request)
{
    set_security_headers(request);
    httpd_resp_set_type(request, "application/json");
    std::array<char, 200> response{};
    const char* name = active_manager == nullptr ? "" : active_manager->access_point_name();
    std::snprintf(response.data(), response.size(),
                  "{\"provisioning\":true,\"ap\":\"%s\",\"ip\":\"%s\",\"session\":\"%08lX\",\"staged\":%s}",
                  name, kPortalIp,
                  static_cast<unsigned long>(active_manager == nullptr ? 0 : active_manager->session_token()),
                  active_manager != nullptr && active_manager->has_valid_stage() ? "true" : "false");
    return httpd_resp_send(request, response.data(), HTTPD_RESP_USE_STRLEN);
}

const char* role_name(config::DeviceRole role)
{
    return role == config::DeviceRole::Receiver ? "rx" : "tx";
}

esp_err_t current_config_handler(httpd_req_t* request)
{
    if (active_manager == nullptr) {
        return json_error(request, "503 Service Unavailable", "{\"error\":\"not_running\"}");
    }
    const config::AppConfig& value = active_manager->visible_configuration();
    cJSON* root = cJSON_CreateObject();
    cJSON* values = cJSON_CreateObject();
    if (root == nullptr || values == nullptr) {
        cJSON_Delete(root);
        cJSON_Delete(values);
        return json_error(request, "503 Service Unavailable", "{\"error\":\"memory\"}");
    }
    cJSON_AddItemToObject(root, "values", values);
    cJSON_AddBoolToObject(root, "configured", value.configured);
    cJSON_AddBoolToObject(root, "staged", active_manager->has_valid_stage());
    cJSON_AddBoolToObject(root, "wifi_password_set", value.wifi.password[0] != '\0');
    cJSON_AddBoolToObject(root, "blynk_token_set", value.blynk.auth_token[0] != '\0');
    const auto paired = std::find_if(value.pairing.peers.begin(), value.pairing.peers.end(), [](const auto& peer) {
        return peer.enabled;
    });
    cJSON_AddBoolToObject(root, "paired", paired != value.pairing.peers.end());
    if (paired != value.pairing.peers.end()) {
        std::array<char, 17> node_id{};
        std::snprintf(node_id.data(), node_id.size(), "%016llX",
                      static_cast<unsigned long long>(paired->node_id));
        cJSON_AddStringToObject(root, "paired_node_id", node_id.data());
    }

#define ADD_NUMBER(name, field) cJSON_AddNumberToObject(values, name, field)
#define ADD_BOOL(name, field) cJSON_AddBoolToObject(values, name, field)
#define ADD_TEXT(name, field) cJSON_AddStringToObject(values, name, field)
    ADD_TEXT("role", role_name(value.device.role));
    ADD_BOOL("display_enabled", value.device.display_enabled);
    ADD_NUMBER("display_timeout_s", value.device.display_timeout_seconds);
    ADD_NUMBER("long_press_ms", value.device.provisioning_long_press_ms);
    ADD_TEXT("wifi_ssid", value.wifi.ssid.data());
    ADD_TEXT("hostname", value.wifi.hostname.data());
    ADD_NUMBER("wifi_retry_min_ms", value.wifi.reconnect_minimum_ms);
    ADD_NUMBER("wifi_retry_max_ms", value.wifi.reconnect_maximum_ms);
    ADD_TEXT("radio_chip", value.radio.chip == config::RadioChip::Sx1276 ? "sx1276" : "sx1278");
    ADD_NUMBER("frequency_hz", value.radio.frequency_hz);
    ADD_NUMBER("bandwidth_hz", value.radio.bandwidth_hz);
    ADD_NUMBER("spreading_factor", value.radio.spreading_factor);
    ADD_NUMBER("coding_rate", value.radio.coding_rate_denominator);
    ADD_NUMBER("sync_word", value.radio.sync_word);
    ADD_NUMBER("preamble", value.radio.preamble_symbols);
    ADD_NUMBER("tx_power", value.radio.transmit_power_dbm);
    ADD_NUMBER("rx_timeout_ms", value.radio.receive_timeout_ms);
    ADD_NUMBER("tx_timeout_ms", value.radio.transmit_timeout_ms);
    ADD_NUMBER("sensor_bottom_cm", value.tank.sensor_reference_height_cm);
    ADD_NUMBER("sensor_surface_cm", value.tank.sensor_reference_height_cm - value.tank.full_level_cm);
    ADD_NUMBER("tank_capacity_litres", value.tank.capacity_litres);
    ADD_TEXT("volume_unit", value.tank.display_unit == config::VolumeUnit::UsGallons ? "us_gallons" :
        (value.tank.display_unit == config::VolumeUnit::ImperialGallons ? "imperial_gallons" : "litres"));
    ADD_TEXT("volume_model", value.tank.volume_model == config::VolumeModel::LinearPerCentimetre ? "linear" : "capacity");
    ADD_NUMBER("volume_per_cm", value.tank.volume_litres_per_centimetre);
    ADD_NUMBER("reference_height_cm", value.tank.sensor_reference_height_cm);
    ADD_NUMBER("min_distance_cm", value.tank.minimum_sensor_distance_cm);
    ADD_NUMBER("max_distance_cm", value.tank.maximum_sensor_distance_cm);
    ADD_NUMBER("empty_level_cm", value.tank.empty_level_cm);
    ADD_NUMBER("full_level_cm", value.tank.full_level_cm);
    ADD_NUMBER("sensor_offset_cm", value.tank.sensor_offset_cm);
    ADD_TEXT("range_policy", value.tank.out_of_range_policy == config::OutOfRangePolicy::Clamp ? "clamp" :
        (value.tank.out_of_range_policy == config::OutOfRangePolicy::Report ? "report" : "reject"));
    ADD_NUMBER("trigger_timeout_us", value.sensor.trigger_timeout_us);
    ADD_NUMBER("power_warmup_ms", value.sensor.power_warmup_ms);
    ADD_NUMBER("sample_count", value.sensor.sample_count);
    ADD_NUMBER("inter_sample_ms", value.sensor.inter_sample_delay_ms);
    ADD_NUMBER("max_spread_cm", value.sensor.maximum_sample_spread_cm);
    ADD_NUMBER("measurement_interval_minutes", value.sensor.measurement_interval_seconds / 60U);
    ADD_NUMBER("critical_interval_s", value.sensor.critical_recheck_interval_seconds);
    ADD_NUMBER("transmission_retries", value.sensor.transmission_retries);
    for (std::size_t slot = 0; slot < value.blynk.virtual_pins.size(); ++slot) {
        std::array<char, 24> key{};
        const auto metric = std::find(value.blynk.virtual_pins.begin(), value.blynk.virtual_pins.end(), slot);
        std::snprintf(key.data(), key.size(), "blynk_slot_%u", static_cast<unsigned int>(slot));
        ADD_NUMBER(key.data(), metric == value.blynk.virtual_pins.end() ? -1 :
            static_cast<std::int16_t>(metric - value.blynk.virtual_pins.begin()));
    }
    ADD_NUMBER("battery_low_v", value.battery.low_voltage_threshold);
    ADD_NUMBER("battery_cutoff_v", value.battery.critical_voltage_cutoff);
    ADD_TEXT("battery_percent_model", value.battery.percentage_model == config::BatteryPercentageModel::Hidden ? "hidden" : "linear");
    ADD_NUMBER("battery_empty_v", value.battery.percentage_empty_voltage);
    ADD_NUMBER("battery_full_v", value.battery.percentage_full_voltage);
    ADD_NUMBER("battery_scale", value.battery.calibration_scale);
    ADD_NUMBER("battery_offset_v", value.battery.calibration_offset_volts);
    ADD_NUMBER("battery_capacity_mah", value.battery.nominal_capacity_mah);
    ADD_NUMBER("battery_usable_percent", value.battery.usable_capacity_percent);
    ADD_BOOL("blynk_enabled", value.blynk.enabled);
    ADD_TEXT("blynk_host", value.blynk.host.data());
    ADD_NUMBER("blynk_port", value.blynk.port);
    ADD_TEXT("blynk_template", value.blynk.template_id.data());
    ADD_TEXT("blynk_device", value.blynk.device_name.data());
    ADD_NUMBER("blynk_interval_s", value.blynk.publish_interval_seconds);
#undef ADD_NUMBER
#undef ADD_BOOL
#undef ADD_TEXT

    char* encoded = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (encoded == nullptr) {
        return json_error(request, "503 Service Unavailable", "{\"error\":\"memory\"}");
    }
    set_security_headers(request);
    httpd_resp_set_type(request, "application/json");
    const esp_err_t error = httpd_resp_send(request, encoded, HTTPD_RESP_USE_STRLEN);
    cJSON_free(encoded);
    return error;
}

bool request_session_valid(httpd_req_t* request)
{
    std::array<char, 16> token{};
    return active_manager != nullptr &&
        httpd_req_get_hdr_value_str(request, "X-LTM-Session", token.data(), token.size()) == ESP_OK &&
        active_manager->session_matches(token.data());
}

esp_err_t json_error(httpd_req_t* request, const char* status, const char* body)
{
    set_security_headers(request);
    httpd_resp_set_status(request, status);
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_send(request, body, HTTPD_RESP_USE_STRLEN);
}

esp_err_t scan_handler(httpd_req_t* request)
{
    if (!request_session_valid(request)) {
        return json_error(request, "403 Forbidden", "{\"error\":\"session\"}");
    }
    bool expected = false;
    if (!scan_in_progress.compare_exchange_strong(expected, true)) {
        return json_error(request, "409 Conflict", "{\"error\":\"scan_in_progress\"}");
    }
    wifi_scan_config_t configuration{};
    configuration.show_hidden = false;
    configuration.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    const esp_err_t error = esp_wifi_scan_start(&configuration, false);
    if (error != ESP_OK) {
        scan_in_progress.store(false);
        return json_error(request, "503 Service Unavailable", "{\"error\":\"scan_failed\"}");
    }
    set_security_headers(request);
    httpd_resp_set_status(request, "202 Accepted");
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_send(request, "{\"scanning\":true}", HTTPD_RESP_USE_STRLEN);
}

bool append_json_text(char* output, std::size_t capacity, std::size_t& size, const char* text)
{
    for (const unsigned char character : std::string_view(text)) {
        const char* escape = nullptr;
        if (character == '"') escape = "\\\"";
        else if (character == '\\') escape = "\\\\";
        if (escape != nullptr) {
            const std::size_t length = std::strlen(escape);
            if (size + length >= capacity) return false;
            std::memcpy(output + size, escape, length);
            size += length;
        } else if (character >= 0x20 && character < 0x7f) {
            if (size + 1 >= capacity) return false;
            output[size++] = static_cast<char>(character);
        }
    }
    output[size] = '\0';
    return true;
}

esp_err_t networks_handler(httpd_req_t* request)
{
    auto response = std::unique_ptr<char[]>(new (std::nothrow) char[4096]{});
    if (!response) {
        return json_error(request, "503 Service Unavailable", "{\"error\":\"memory\"}");
    }
    std::array<NetworkResult, kMaximumNetworks> snapshot{};
    std::size_t count = 0;
    portENTER_CRITICAL(&network_lock);
    count = network_count;
    std::copy_n(networks.begin(), count, snapshot.begin());
    portEXIT_CRITICAL(&network_lock);

    std::size_t size = static_cast<std::size_t>(std::snprintf(
        response.get(), 4096, "{\"scanning\":%s,\"networks\":[",
        scan_in_progress.load() ? "true" : "false"));
    for (std::size_t index = 0; index < count; ++index) {
        if (index != 0 && size + 1 < 4096) response[size++] = ',';
        if (size + 10 >= 4096) break;
        std::memcpy(response.get() + size, "{\"ssid\":\"", 9);
        size += 9;
        if (!append_json_text(response.get(), 4096, size, snapshot[index].ssid.data())) break;
        const int written = std::snprintf(response.get() + size, 4096 - size,
                                          "\",\"rssi\":%d,\"secure\":%s}",
                                          snapshot[index].rssi,
                                          snapshot[index].authentication == WIFI_AUTH_OPEN ? "false" : "true");
        if (written < 0 || static_cast<std::size_t>(written) >= 4096 - size) break;
        size += static_cast<std::size_t>(written);
    }
    if (size + 3 < 4096) {
        std::memcpy(response.get() + size, "]}", 3);
    }
    set_security_headers(request);
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_send(request, response.get(), HTTPD_RESP_USE_STRLEN);
}

esp_err_t stage_handler(httpd_req_t* request)
{
    if (!request_session_valid(request)) {
        return json_error(request, "403 Forbidden", "{\"error\":\"session\"}");
    }
    if (request->content_len <= 0 || request->content_len > static_cast<int>(kMaximumFormBodySize)) {
        return json_error(request, "413 Content Too Large", "{\"error\":\"size\"}");
    }
    auto body = std::unique_ptr<char[]>(new (std::nothrow) char[request->content_len + 1]{});
    if (!body) {
        return json_error(request, "503 Service Unavailable", "{\"error\":\"memory\"}");
    }
    int received_total = 0;
    while (received_total < request->content_len) {
        const int received = httpd_req_recv(
            request, body.get() + received_total, request->content_len - received_total);
        if (received <= 0) {
            return json_error(request, "400 Bad Request", "{\"error\":\"receive\"}");
        }
        received_total += received;
    }
    const FormResult result = active_manager->stage(body.get(), received_total);
    std::array<char, 128> response{};
    std::snprintf(response.data(), response.size(),
                  "{\"valid\":%s,\"error\":%u,\"validation_error\":%u}",
                  result.valid() ? "true" : "false",
                  static_cast<unsigned int>(result.error),
                  static_cast<unsigned int>(result.validation_error));
    set_security_headers(request);
    httpd_resp_set_status(request, result.valid() ? "200 OK" : "422 Unprocessable Content");
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_send(request, response.data(), HTTPD_RESP_USE_STRLEN);
}

void reboot_task(void*)
{
    vTaskDelay(pdMS_TO_TICKS(750));
    esp_restart();
}

bool schedule_reboot()
{
    return xTaskCreate(reboot_task, "portal_reboot", 2048, nullptr, 3, nullptr) == pdPASS;
}

esp_err_t save_handler(httpd_req_t* request)
{
    if (!request_session_valid(request)) {
        return json_error(request, "403 Forbidden", "{\"error\":\"session\"}");
    }
    if (active_manager == nullptr || !active_manager->has_valid_stage()) {
        return json_error(request, "409 Conflict", "{\"error\":\"not_staged\"}");
    }
    const storage::StoreStatus status = active_manager->save_staged();
    if (status != storage::StoreStatus::Ok || !schedule_reboot()) {
        return json_error(request, "500 Internal Server Error", "{\"saved\":false}");
    }
    set_security_headers(request);
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_send(request, "{\"saved\":true}", HTTPD_RESP_USE_STRLEN);
}

esp_err_t factory_reset_handler(httpd_req_t* request)
{
    std::array<char, 8> confirmation{};
    if (!request_session_valid(request) ||
        httpd_req_get_hdr_value_str(request, "X-LTM-Confirm", confirmation.data(), confirmation.size()) != ESP_OK ||
        std::strcmp(confirmation.data(), "ERASE") != 0) {
        return json_error(request, "403 Forbidden", "{\"error\":\"confirmation\"}");
    }
    const storage::StoreStatus status = active_manager->factory_reset();
    if (status != storage::StoreStatus::Ok || !schedule_reboot()) {
        return json_error(request, "500 Internal Server Error", "{\"reset\":false}");
    }
    set_security_headers(request);
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_send(request, "{\"reset\":true}", HTTPD_RESP_USE_STRLEN);
}

esp_err_t clear_pairing_handler(httpd_req_t* request)
{
    std::array<char, 8> confirmation{};
    if (!request_session_valid(request) ||
        httpd_req_get_hdr_value_str(request, "X-LTM-Confirm", confirmation.data(), confirmation.size()) != ESP_OK ||
        std::strcmp(confirmation.data(), "CLEAR") != 0) {
        return json_error(request, "403 Forbidden", "{\"error\":\"confirmation\"}");
    }
    const storage::StoreStatus status = active_manager->clear_pairing();
    if (status != storage::StoreStatus::Ok || !schedule_reboot()) {
        return json_error(request, "500 Internal Server Error", "{\"cleared\":false}");
    }
    set_security_headers(request);
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_send(request, "{\"cleared\":true}", HTTPD_RESP_USE_STRLEN);
}

esp_err_t redirect_to_portal(httpd_req_t* request)
{
    ESP_LOGI(kLogTag, "Captive HTTP probe redirected: %s", request->uri);
    set_security_headers(request);
    httpd_resp_set_status(request, "302 Found");
    httpd_resp_set_hdr(request, "Location", "http://192.168.4.1/");
    return httpd_resp_send(request, nullptr, 0);
}

esp_err_t not_found_handler(httpd_req_t* request, httpd_err_code_t)
{
    return redirect_to_portal(request);
}

bool append_dns_u16(std::array<std::uint8_t, kDnsPacketMaximum>& packet,
                    std::size_t& size,
                    std::uint16_t value)
{
    if (size + 2 > packet.size()) {
        return false;
    }
    packet[size++] = static_cast<std::uint8_t>(value >> 8U);
    packet[size++] = static_cast<std::uint8_t>(value);
    return true;
}

std::size_t dns_question_end(const std::uint8_t* packet, std::size_t size)
{
    if (packet == nullptr || size < 12) {
        return 0;
    }
    std::size_t offset = 12;
    while (offset < size) {
        const std::uint8_t label_length = packet[offset++];
        if (label_length == 0) {
            return offset + 4 <= size ? offset + 4 : 0;
        }
        if ((label_length & 0xc0U) != 0 || offset + label_length > size) {
            return 0;
        }
        offset += label_length;
    }
    return 0;
}

void dns_task(void*)
{
    const int socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_fd < 0) {
        ESP_LOGE(kLogTag, "DNS socket creation failed: %d", errno);
        dns_running.store(false);
        dns_task_handle = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    const timeval timeout{.tv_sec = 0, .tv_usec = 250000};
    setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(kDnsPort);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(socket_fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        ESP_LOGE(kLogTag, "DNS bind failed: %d", errno);
        close(socket_fd);
        dns_running.store(false);
        dns_task_handle = nullptr;
        vTaskDelete(nullptr);
        return;
    }
    ESP_LOGI(kLogTag, "Captive DNS listening on UDP/53");

    std::array<std::uint8_t, kDnsPacketMaximum> packet{};
    while (dns_running.load()) {
        sockaddr_in client{};
        socklen_t client_length = sizeof(client);
        const int received = recvfrom(socket_fd, packet.data(), packet.size(), 0,
                                      reinterpret_cast<sockaddr*>(&client), &client_length);
        if (received <= 0) {
            continue;
        }
        const std::size_t received_size = static_cast<std::size_t>(received);
        const std::size_t question_end = dns_question_end(packet.data(), received_size);
        if (question_end == 0 || packet[4] != 0 || packet[5] != 1 || (packet[2] & 0x80U) != 0) {
            continue;
        }

        const std::uint16_t question_type = static_cast<std::uint16_t>(packet[question_end - 4] << 8U) |
            packet[question_end - 3];
        const std::uint16_t question_class = static_cast<std::uint16_t>(packet[question_end - 2] << 8U) |
            packet[question_end - 1];
        const bool answer_with_portal_ip = question_type == 1 && question_class == 1;
        const std::uint8_t recursion_desired = packet[2] & 0x01U;
        packet[2] = static_cast<std::uint8_t>(0x84U | recursion_desired);
        packet[3] = 0;
        packet[6] = 0;
        packet[7] = answer_with_portal_ip ? 1 : 0;
        packet[8] = packet[9] = packet[10] = packet[11] = 0;
        std::size_t response_size = question_end;
        if (answer_with_portal_ip) {
            const bool appended =
                append_dns_u16(packet, response_size, 0xc00c) &&
                append_dns_u16(packet, response_size, 1) &&
                append_dns_u16(packet, response_size, 1) &&
                append_dns_u16(packet, response_size, 0) &&
                append_dns_u16(packet, response_size, 0) &&
                append_dns_u16(packet, response_size, 4);
            if (!appended || response_size + 4 > packet.size()) {
                continue;
            }
            packet[response_size++] = 192;
            packet[response_size++] = 168;
            packet[response_size++] = 4;
            packet[response_size++] = 1;
        }
        ESP_LOGI(kLogTag, "Captive DNS query type=%u class=%u", question_type, question_class);
        if (sendto(socket_fd, packet.data(), response_size, 0,
                   reinterpret_cast<const sockaddr*>(&client), client_length) < 0) {
            ESP_LOGW(kLogTag, "Captive DNS reply failed: %d", errno);
        }
    }
    close(socket_fd);
    dns_task_handle = nullptr;
    vTaskDelete(nullptr);
}

bool start_http_server()
{
    httpd_config_t configuration = HTTPD_DEFAULT_CONFIG();
    configuration.max_uri_handlers = 17;
    configuration.stack_size = 8192;
    if (httpd_start(&http_server, &configuration) != ESP_OK) {
        return false;
    }
    const httpd_uri_t root{.uri = "/", .method = HTTP_GET, .handler = root_handler, .user_ctx = nullptr};
    const httpd_uri_t status{.uri = "/api/status", .method = HTTP_GET, .handler = status_handler, .user_ctx = nullptr};
    const httpd_uri_t current_config{.uri = "/api/config/current", .method = HTTP_GET, .handler = current_config_handler, .user_ctx = nullptr};
    const httpd_uri_t scan{.uri = "/api/wifi/scan", .method = HTTP_POST, .handler = scan_handler, .user_ctx = nullptr};
    const httpd_uri_t network_list{.uri = "/api/wifi/networks", .method = HTTP_GET, .handler = networks_handler, .user_ctx = nullptr};
    const httpd_uri_t stage{.uri = "/api/config/stage", .method = HTTP_POST, .handler = stage_handler, .user_ctx = nullptr};
    const httpd_uri_t save{.uri = "/api/config/save", .method = HTTP_POST, .handler = save_handler, .user_ctx = nullptr};
    const httpd_uri_t clear_pairing{.uri = "/api/pairing/clear", .method = HTTP_POST, .handler = clear_pairing_handler, .user_ctx = nullptr};
    const httpd_uri_t factory_reset{.uri = "/api/factory-reset", .method = HTTP_POST, .handler = factory_reset_handler, .user_ctx = nullptr};
    const std::array<const char*, 6> captive_paths{
        "/generate_204", "/gen_204", "/hotspot-detect.html", "/connecttest.txt", "/ncsi.txt", "/redirect"};
    if (httpd_register_uri_handler(http_server, &root) != ESP_OK ||
        httpd_register_uri_handler(http_server, &status) != ESP_OK ||
        httpd_register_uri_handler(http_server, &current_config) != ESP_OK ||
        httpd_register_uri_handler(http_server, &scan) != ESP_OK ||
        httpd_register_uri_handler(http_server, &network_list) != ESP_OK ||
        httpd_register_uri_handler(http_server, &stage) != ESP_OK ||
        httpd_register_uri_handler(http_server, &save) != ESP_OK ||
        httpd_register_uri_handler(http_server, &clear_pairing) != ESP_OK ||
        httpd_register_uri_handler(http_server, &factory_reset) != ESP_OK) {
        return false;
    }
    for (const char* path : captive_paths) {
        const httpd_uri_t redirect{.uri = path, .method = HTTP_GET, .handler = redirect_to_portal, .user_ctx = nullptr};
        if (httpd_register_uri_handler(http_server, &redirect) != ESP_OK) {
            return false;
        }
    }
    return httpd_register_err_handler(http_server, HTTPD_404_NOT_FOUND, not_found_handler) == ESP_OK;
}

bool initialize_wifi_stack()
{
    esp_err_t error = nvs_flash_init();
    if (error == ESP_ERR_NVS_NO_FREE_PAGES || error == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        error = nvs_flash_erase();
        if (error == ESP_OK) {
            error = nvs_flash_init();
        }
    }
    if (error != ESP_OK) {
        return false;
    }
    error = esp_netif_init();
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) {
        return false;
    }
    error = esp_event_loop_create_default();
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) {
        return false;
    }
    if (esp_netif_create_default_wifi_ap() == nullptr) {
        return false;
    }
    if (esp_netif_create_default_wifi_sta() == nullptr) {
        return false;
    }
    const wifi_init_config_t wifi_initialization = WIFI_INIT_CONFIG_DEFAULT();
    return esp_wifi_init(&wifi_initialization) == ESP_OK &&
        esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, nullptr) == ESP_OK &&
        esp_wifi_set_storage(WIFI_STORAGE_RAM) == ESP_OK;
}

}  // namespace

bool ProvisioningManager::start(
    std::uint64_t hardware_node_id,
    const config::AppConfig& base_configuration,
    storage::ConfigStore& config_store)
{
    if (running_) {
        return true;
    }
    base_configuration_ = base_configuration;
    config_store_ = &config_store;
    hardware_node_id_ = hardware_node_id;
    session_token_ = esp_random();
    if (session_token_ == 0) {
        session_token_ = 1;
    }
    std::snprintf(access_point_name_.data(), access_point_name_.size(),
                  "LoRaTank-%04llX",
                  static_cast<unsigned long long>(hardware_node_id & 0xffffU));
    if (!initialize_wifi_stack() || esp_wifi_set_mode(WIFI_MODE_APSTA) != ESP_OK) {
        ESP_LOGE(kLogTag, "Wi-Fi initialization failed");
        return false;
    }

    wifi_config_t wifi_configuration{};
    const std::size_t name_length = std::min(
        std::strlen(access_point_name_.data()), sizeof(wifi_configuration.ap.ssid));
    std::memcpy(wifi_configuration.ap.ssid, access_point_name_.data(), name_length);
    wifi_configuration.ap.ssid_len = static_cast<std::uint8_t>(name_length);
    wifi_configuration.ap.channel = 1;
    wifi_configuration.ap.authmode = WIFI_AUTH_OPEN;
    wifi_configuration.ap.max_connection = 4;
    wifi_configuration.ap.beacon_interval = 100;
    if (esp_wifi_set_config(WIFI_IF_AP, &wifi_configuration) != ESP_OK ||
        esp_wifi_start() != ESP_OK) {
        ESP_LOGE(kLogTag, "Provisioning AP start failed");
        return false;
    }

    active_manager = this;
    if (!start_http_server()) {
        ESP_LOGE(kLogTag, "HTTP server start failed");
        stop();
        return false;
    }
    dns_running.store(true);
    if (xTaskCreate(dns_task, "captive_dns", 3072, nullptr, 4, &dns_task_handle) != pdPASS) {
        ESP_LOGE(kLogTag, "DNS task start failed");
        dns_running.store(false);
        stop();
        return false;
    }

    running_ = true;
    ESP_LOGW(kLogTag, "Open provisioning AP started: %s", access_point_name_.data());
    ESP_LOGI(kLogTag, "Portal: http://%s/", kPortalIp);
    return true;
}

FormResult ProvisioningManager::stage(const char* body, std::size_t size)
{
    FormResult result = parse_config_form(body, size, base_configuration_, hardware_node_id_);
    has_valid_stage_ = result.valid();
    if (result.valid()) {
        staged_configuration_ = result.configuration;
        ESP_LOGI(kLogTag, "Configuration staged: role=%u tank=%s",
                 static_cast<unsigned int>(staged_configuration_.device.role),
                 staged_configuration_.tank.tank_name.data());
    } else {
        ESP_LOGW(kLogTag, "Configuration stage rejected: form=%u validation=%u",
                 static_cast<unsigned int>(result.error),
                 static_cast<unsigned int>(result.validation_error));
    }
    return result;
}

storage::StoreStatus ProvisioningManager::save_staged()
{
    if (!has_valid_stage_ || config_store_ == nullptr) {
        return storage::StoreStatus::InvalidRecord;
    }
    const storage::StoreStatus status = config_store_->save(staged_configuration_);
    if (status == storage::StoreStatus::Ok) {
        base_configuration_ = staged_configuration_;
    }
    return status;
}

storage::StoreStatus ProvisioningManager::clear_pairing()
{
    if (config_store_ == nullptr) {
        return storage::StoreStatus::NotInitialized;
    }
    config::AppConfig updated = visible_configuration();
    updated.pairing = {};
    const storage::StoreStatus status = config_store_->save(updated);
    if (status == storage::StoreStatus::Ok) {
        base_configuration_ = updated;
        staged_configuration_ = updated;
        has_valid_stage_ = true;
    }
    return status;
}

storage::StoreStatus ProvisioningManager::factory_reset()
{
    return config_store_ == nullptr
        ? storage::StoreStatus::NotInitialized
        : config_store_->factory_reset();
}

bool ProvisioningManager::session_matches(const char* value) const
{
    if (value == nullptr) {
        return false;
    }
    std::array<char, 9> expected{};
    std::snprintf(expected.data(), expected.size(), "%08lX", static_cast<unsigned long>(session_token_));
    return std::strcmp(value, expected.data()) == 0;
}

void ProvisioningManager::stop()
{
    dns_running.store(false);
    if (http_server != nullptr) {
        httpd_stop(http_server);
        http_server = nullptr;
    }
    static_cast<void>(esp_wifi_stop());
    active_manager = nullptr;
    running_ = false;
}

bool ProvisioningManager::running() const
{
    return running_;
}

bool ProvisioningManager::has_valid_stage() const
{
    return has_valid_stage_;
}

const config::AppConfig& ProvisioningManager::visible_configuration() const
{
    return has_valid_stage_ ? staged_configuration_ : base_configuration_;
}

std::uint32_t ProvisioningManager::session_token() const
{
    return session_token_;
}

const char* ProvisioningManager::access_point_name() const
{
    return access_point_name_.data();
}

const char* ProvisioningManager::ip_address() const
{
    return kPortalIp;
}

}  // namespace tank_monitor::provisioning