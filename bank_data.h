#pragma once
#include <stdint.h>

// === DATABASE KARTU INDONESIA ===

// 1. MANDIRI E-MONEY (New Gen / CPU)
// AID: A00000000386980701
const uint8_t AID_MANDIRI[] = {
    0xA0, 0x00, 0x00, 0x00, 0x03, 0x86, 0x98, 0x07, 0x01
};
// Command Cek Saldo (Standard Read Binary)
const uint8_t CMD_BAL_MANDIRI[] = {
    0x00, 0xB0, 0x00, 0x00, 0x04
};

// 2. BCA FLAZZ (Gen 2)
// AID: A0000000041010
const uint8_t AID_BCA[] = {
    0xA0, 0x00, 0x00, 0x00, 0x04, 0x10, 0x10
};
// Command Cek Saldo (Placeholder / Generic)
const uint8_t CMD_BAL_BCA[] = {
    0x80, 0x5C, 0x00, 0x02, 0x04
};