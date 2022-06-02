#include "si7210_defs.h"
#include "si7210.h"
#include "fsl_clock.h"
#include "SMBus.h"
#include "openthread/platform/logging.h"
#include "../include/app_config.h"
si7210_status_t usr_i2c_read(uint8_t dev_id, uint8_t reg_addr, uint8_t *data, uint16_t len);
si7210_status_t usr_i2c_write(uint8_t dev_id, uint8_t reg_addr, uint8_t *data, uint16_t len);
void usr_delay_ms(uint32_t period_ms);
si7210_dev_t dev = {
    .dev_id   = SI7210_ADDRESS_0,
    .read     = usr_i2c_read,
    .write    = usr_i2c_write,
    .delay_ms = usr_delay_ms,
//  .callback = callback,
};

si7210_status_t doorlockInit()
{
si7210_status_t rslt = SI7210_OK;

vSMBusInit();
rslt = si7210_init(&dev);

if(rslt != SI7210_OK)
{
    K32W_LOG("si7210_init error");
}
return rslt;
}


si7210_status_t doorlockRead(float* field_strength, float* temperature)
{
si7210_status_t rslt = SI7210_OK;
dev.settings.range        = SI7210_20mT;
dev.settings.compensation = SI7210_COMPENSATION_TEMP_NEO;
dev.settings.output_pin   = SI7210_OUTPUT_PIN_LOW;
 
if((rslt = si7210_set_sensor_settings(&dev)) != SI7210_OK)
    return rslt;
else 
{
    /* Obtain field strength reading from device */
    si7210_get_field_strength(&dev, field_strength);

    /* Obtain a temperature reading from the device */
    si7210_get_temperature(&dev, temperature);

    return rslt;
}
}

si7210_status_t usr_i2c_read(uint8_t dev_id, uint8_t reg_addr, uint8_t *data, uint16_t len)
{
    si7210_status_t rslt = SI7210_OK;

    /* User implemented I2C read function */
    bSMBusRandomRead(dev_id,reg_addr,len,data);

    return rslt;
}

si7210_status_t usr_i2c_write(uint8_t dev_id, uint8_t reg_addr, uint8_t *data, uint16_t len)
{
    si7210_status_t rslt = SI7210_OK;

    /* User implemented I2C write function */
    bSMBusWrite(dev_id,reg_addr,len,data);

    return rslt;
}

void usr_delay_ms(uint32_t period_ms)
{
    /* User implemented delay (ms) function */
    CLOCK_uDelay(period_ms*1000);
}

