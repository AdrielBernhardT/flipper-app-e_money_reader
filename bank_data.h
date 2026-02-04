#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    const char* name;         
    const uint8_t* aid;         
    uint8_t aid_len;
    const uint8_t* cmd_bal;    
    uint8_t cmd_len;
    bool is_little_endian;     
    uint8_t offset;             
} BankProfile;

// --- 1. MANDIRI E-MONEY ---
const uint8_t AID_MANDIRI[] = {0xA0, 0x00, 0x00, 0x00, 0x03, 0x86, 0x98, 0x07, 0x01};
const uint8_t CMD_MANDIRI[] = {0x00, 0xB0, 0x00, 0x00, 0x04};

// --- 2. BCA FLAZZ (GEN 2 - DESFire) ---
const uint8_t AID_BCA[] = {0xA0, 0x00, 0x00, 0x00, 0x04, 0x10, 0x10};
const uint8_t CMD_BCA[] = {0x80, 0x5C, 0x00, 0x02, 0x04}; 

// --- 3. BRI BRIZZI ---
const uint8_t AID_BRI[] = {0xA0, 0x00, 0x00, 0x00, 0x03, 0x86, 0x98, 0x07, 0x02};
const uint8_t CMD_BRI[] = {0x00, 0xB0, 0x00, 0x00, 0x03}; 

// --- 4. BNI TAPCASH ---
const uint8_t AID_BNI[] = {0xA0, 0x00, 0x00, 0x06, 0x02, 0x10, 0x03}; 
const uint8_t CMD_BNI[] = {0x90, 0x32, 0x03, 0x00, 0x04}; 

// === LIST REGISTER ===
#define BANK_COUNT 4

static const BankProfile BANK_DB[BANK_COUNT] = {
    {"MANDIRI", AID_MANDIRI, sizeof(AID_MANDIRI), CMD_MANDIRI, sizeof(CMD_MANDIRI), true, 0},
    {"BCA FLAZZ", AID_BCA, sizeof(AID_BCA), CMD_BCA, sizeof(CMD_BCA), false, 0},
    {"BRIZZI", AID_BRI, sizeof(AID_BRI), CMD_BRI, sizeof(CMD_BRI), true, 0},
    {"BNI TAPCASH", AID_BNI, sizeof(AID_BNI), CMD_BNI, sizeof(CMD_BNI), false, 0},
};