#
#
#

#
# Options.
#

cmake_minimum_required(VERSION 3.20.0)

#
set(BOARD nrf52840dk_nrf52840)
# set(BOARD xiao_ble)

set(uav_operator GBR-OP-ABCD12345678)
set(uav_id ZZZZH1234567)
set(self_id CAA\ UAS\ 7068)
# ODID_UATYPE_AEROPLANE
set(odid_uatype ODID_UATYPE_NONE)
# ODID_CATEGORY_EU_UNDECLARED
set(odid_cat ODID_CATEGORY_EU_SPECIFIC)

## Added by Sandoche 
# set(det_ipv6 "0x20,0x01,0x00,0x30,0x3f,0xf8,0x04,0x02,0xa4,0x6f,0xb5,0x09,0xaf,0xce,0x2c,0xb0,0x00,0x00,0x00,0x00")
set(det_ipv6 "0x20,0x01,0x00,0x30,0x3f,0xf8,0x04,0x02,0x73,0xc6,0x58,0x1c,0xcc,0xde,0x39,0x98,0x00,0x00,0x00,0x00")
# set(det_ipv6 "0x20,0x01,0x00,0x30,0x3f,0xf8,0x04,0x05,0xd9,0x52,0x56,0x18,0xfb,0xc9,0xc3,0xcf,0x00,0x00,0x00,0x00")

# National/regional variations.
set(id_japan 0)
set(id_usa   0)

# Experimental.
set(spektrum   0)
set(batt_volts 0)

set(CMAKE_C_FLAGS   "-Wall -Wno-parentheses -Wno-deprecated-declarations")
set(CMAKE_CXX_FLAGS "-Wall -Wno-parentheses -Wno-deprecated-declarations")

#
# Build.
#

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(remote_id_nrf52)

# Source files.

# Unix epoch (seconds) at configure time. The board has no RTC, so this is injected as the
# transmitter's real-time base: timestamps become ~real at flash without manual editing.
string(TIMESTAMP build_unix_time "%s" UTC)

configure_file(src/remote_id.h.in ${CMAKE_CURRENT_SOURCE_DIR}/src/remote_id.h)

set(c_sources src/opendroneid.c src/alt_unix_time.c)

set(cpp_sources src/main_nrf52.cpp src/gps2.cpp src/odid_bt.cpp)

if (${batt_volts})
  set(cpp_sources ${cpp_sources} src/battery.cpp)
endif()

if (${id_japan})
  set(cpp_sources ${cpp_sources} src/local.cpp)
endif()

if (${spektrum})
  set(cpp_sources ${cpp_sources} src/spektrum_i2c.cpp)
endif()

target_sources(app PRIVATE ${cpp_sources} ${c_sources})
