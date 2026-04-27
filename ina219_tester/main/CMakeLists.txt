# See the build system documentation in IDF programming guide
# for more information about component CMakeLists.txt files.

idf_component_register(
    SRCS main.c         # list the source files of this component
    INCLUDE_DIRS "."       # optional, add here public include directories
    PRIV_INCLUDE_DIRS   # optional, add here private include directories
    REQUIRES            # optional, list the public requirements (component names)
    PRIV_REQUIRES  	ina219
    				i2cdev 
    				esp_idf_lib_helpers
    				esp_adc
    				esp_timer
    				driver     # optional, list the private requirements
)
