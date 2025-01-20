#include "mcp4725.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"

#define MCP4725_FAST_MODE          0x00
#define MCP4725_WRITE_DAC_REGISTER 0x40
#define MCP4725_WRITE_EEPROM       0x60
#define MCP4725_I2C_TIMEOUT        1000000  // 1 second timeout in microseconds

int mcp4725_init(mcp4725_handle_t *handle, i2c_inst_t *i2c, uint8_t i2c_address) {
    if (!handle || !i2c) return -1;

    handle->refVoltage = 3.3;
    handle->bitsPerVolt = 4096 / handle->refVoltage;
    handle->i2c = i2c;
    handle->i2c_address = i2c_address;

    return 0;
}

int mcp4725_deinit(mcp4725_handle_t const *handle) {
    if (!handle) return -1;
    return 0;  // No specific deinitialization needed
}

int mcp4725_write_eeprom(mcp4725_handle_t const *handle, uint16_t dac_value, uint8_t power_down_mode) {
    if (!handle || dac_value > 0xFFF) return -1;

    uint8_t buffer[3];
    buffer[0] = MCP4725_WRITE_EEPROM | (power_down_mode << 4);
    buffer[1] = (dac_value >> 4) & 0xFF;
    buffer[2] = (dac_value & 0x0F) << 4;

    int ret = i2c_write_timeout_us(handle->i2c, handle->i2c_address, buffer, 3, false, MCP4725_I2C_TIMEOUT);
    return ret < 0 ? -2 : 0;
}

int mcp4725_read(mcp4725_handle_t const *handle, uint16_t *dac_value, uint8_t *power_down_mode, bool *eeprom_ready) {
    if (!handle || !dac_value || !power_down_mode || !eeprom_ready) return -1;

    uint8_t buffer[5] = {0};
    int ret = i2c_read_timeout_us(handle->i2c, handle->i2c_address, buffer, 5, false, MCP4725_I2C_TIMEOUT);
    if (ret < 0) return -2;

    *power_down_mode = (buffer[0] >> 1) & 0x03;
    *dac_value = ((buffer[1] << 8) | buffer[2]) >> 4;
    *eeprom_ready = (buffer[0] & 0x80) != 0;

    return 0;
}

int mcp4725_set_voltage(mcp4725_handle_t const *handle, float voltage, uint8_t power_down_mode) {
    if (!handle || voltage < 0 || voltage > handle->refVoltage) return -1;

    uint16_t dac_value = (uint16_t)(voltage * handle->bitsPerVolt);
    uint8_t buffer[3];
    buffer[0] = MCP4725_FAST_MODE | (power_down_mode << 4);
    buffer[1] = (dac_value >> 4) & 0xFF;
    buffer[2] = (dac_value & 0x0F) << 4;

    int ret = i2c_write_timeout_us(handle->i2c, handle->i2c_address, buffer, 3, false, MCP4725_I2C_TIMEOUT);
    return ret < 0 ? -2 : 0;
}

int mcp4725_get_voltage(mcp4725_handle_t const *handle, float *voltage) {
    if (!handle || !voltage) return -1;

    uint16_t dac_value = 0;
    uint8_t power_down_mode = 0;
    bool eeprom_ready = false;

    int ret = mcp4725_read(handle, &dac_value, &power_down_mode, &eeprom_ready);
    if (ret != 0) return ret;

    *voltage = dac_value / handle->bitsPerVolt;
    return 0;
}
