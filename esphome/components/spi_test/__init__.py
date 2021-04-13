import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import spi
from esphome.const import CONF_ID

DEPENDENCIES = ["spi"]
AUTO_LOAD = ["sensor"]
MULTI_CONF = True

CONF_SPI_TEST = "spi_test"

spi_test_ns = cg.esphome_ns.namespace("spi_test")
SPI_TEST = spi_test_ns.class_("SPI_TEST", cg.Component, spi.SPIDevice)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(SPI_TEST),
    }
).extend(spi.spi_device_schema(cs_pin_required=True))


def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    yield cg.register_component(var, config)
    yield spi.register_spi_device(var, config)
