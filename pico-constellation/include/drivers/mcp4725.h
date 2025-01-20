#ifndef MCP4725_H
#define MCP4725_H

#include "hardware/i2c.h"

typedef struct
{
    float refVoltage;    // Reference voltage for calculations
    float bitsPerVolt;   // Precalculated value: 4096 / refVoltage
    i2c_inst_t *i2c;     // I2C instance
    uint8_t i2c_address; // I2C address of the MCP4725
} mcp4725_handle_t;

// Power-down modes
#define MCP4725_PowerDownOff 0x00
#define MCP4725_PowerDown1kOhm 0x01
#define MCP4725_PowerDown100kOhm 0x02
#define MCP4725_PowerDown500kOhm 0x03

// Function prototypes
int mcp4725_init(mcp4725_handle_t *handle, i2c_inst_t *i2c, uint8_t i2c_address);
int mcp4725_write_eeprom(mcp4725_handle_t const *handle, uint16_t dac_value, uint8_t power_down_mode);
int mcp4725_read(mcp4725_handle_t const *handle, uint16_t *dac_value, uint8_t *power_down_mode, bool *eeprom_ready);
int mcp4725_set_voltage(mcp4725_handle_t const *handle, float voltage, uint8_t power_down_mode);
int mcp4725_get_voltage(mcp4725_handle_t const *handle, float *voltage);
int mcp4725_deinit(mcp4725_handle_t const *handle);

#endif // MCP4725_H
