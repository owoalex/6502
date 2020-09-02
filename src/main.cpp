/*

ZX v1.0.0
Copyright (c) Alex Baldwin 2020.

ZX is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License,
version 2 exclusively, as published by the Free Software Foundation.

ZX is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with ZX. If not, see:
https://www.gnu.org/licenses/old-licenses/gpl-2.0.txt/

*/

#include <iostream>

//#include <gtk/gtk.h>
#include "json.hpp"
#include <stdlib.h>
#include <cstring>
#include <fstream>
#include <streambuf>
#include <iomanip>
#include <sstream>

#include <stdio.h>
#include <unistd.h>

#include <sys/stat.h> // stat
#include <errno.h>    // errno, ENOENT, EEXIST
#if defined(_WIN32)
#include <direct.h>   // _mkdir
#endif

using json = nlohmann::json;

bool isDirExist(const std::string& path)
{
#if defined(_WIN32)
    struct _stat info;
    if (_stat(path.c_str(), &info) != 0)
    {
        return false;
    }
    return (info.st_mode & _S_IFDIR) != 0;
#else 
    struct stat info;
    if (stat(path.c_str(), &info) != 0)
    {
        return false;
    }
    return (info.st_mode & S_IFDIR) != 0;
#endif
}

bool makePath(const std::string& path)
{
#if defined(_WIN32)
    int ret = _mkdir(path.c_str());
#else
    mode_t mode = 0755;
    int ret = mkdir(path.c_str(), mode);
#endif
    if (ret == 0)
        return true;

    switch (errno)
    {
    case ENOENT:
        // parent didn't exist, try to create it
        {
            int pos = path.find_last_of('/');
            if (pos == (int) std::string::npos)
#if defined(_WIN32)
                pos = path.find_last_of('\\');
            if (pos == (int) std::string::npos)
#endif
                return false;
            if (!makePath( path.substr(0, pos) ))
                return false;
        }
        // now, try to create again
#if defined(_WIN32)
        return 0 == _mkdir(path.c_str());
#else 
        return 0 == mkdir(path.c_str(), mode);
#endif

    case EEXIST:
        // done!
        return isDirExist(path);

    default:
        return false;
    }
}

std::string primaryRom = "";
std::string secondaryRom = "";
std::string fullRom = "";

uint8_t virtualMemoryMap[0x010000];

uint8_t virtualAccumulator = 0;
uint8_t virtualXRegister = 0;
uint8_t virtualYRegister = 0;
uint16_t virtualProgramCounter = 0;
uint8_t virtualStackPointer = 0;
uint8_t virtualStatus = 0;
bool halted = true;

uint16_t getShortFromAddress(int addr) {
    return ((uint16_t)virtualMemoryMap[addr]) + (((uint16_t)virtualMemoryMap[addr + 1]) * 256);
}

void setShortAtAddress(int addr, uint16_t value) {
    virtualMemoryMap[addr] = value % 256;
    virtualMemoryMap[addr + 1] = value / 256;
}

uint8_t popByteFromStack() {
    virtualStackPointer++;
    return virtualMemoryMap[0x0100 + virtualStackPointer];
}

uint16_t popShortFromStack() {
    return (((uint16_t) popByteFromStack()) * 256) + ((uint16_t) popByteFromStack());
}

void pushByteToStack(uint8_t value) {
    virtualMemoryMap[0x0100 + virtualStackPointer] = value;
    virtualStackPointer--;
}

void pushShortToStack(uint16_t value) {
    pushByteToStack(value % 256);
    pushByteToStack(value / 256);
}

void clearCarryFlag() {
    virtualStatus &= ~(1UL << 0); // Set bit to 0
}

void setCarryFlag() {
    virtualStatus |= 1UL << 0; // Set bit to 1
}

void clearZeroFlag() {
    virtualStatus &= ~(1UL << 1); // Set bit to 0
}

void setZeroFlag() {
    virtualStatus |= 1UL << 1; // Set bit to 1
}

void clearInterruptDisableStatus() {
    virtualStatus &= ~(1UL << 2); // Set bit to 0
}

void setInterruptDisableStatus() {
    virtualStatus |= 1UL << 2; // Set bit to 1
}

void clearDecimalMode() {
    virtualStatus &= ~(1UL << 3); // Set bit to 0
}

void setDecimalMode() {
    virtualStatus |= 1UL << 3; // Set bit to 1
}

void clearBreakFlag() {
    virtualStatus &= ~(1UL << 4); // Set bit to 0
}

void setBreakFlag() {
    virtualStatus |= 1UL << 4; // Set bit to 1
}

void clearOverflowFlag() {
    virtualStatus &= ~(1UL << 6); // Set bit to 0
}

void setOverflowFlag() {
    virtualStatus |= 1UL << 6; // Set bit to 1
}

void clearNegativeFlag() {
    virtualStatus &= ~(1UL << 7); // Set bit to 0
}

void setNegativeFlag() {
    virtualStatus |= 1UL << 7; // Set bit to 1
}

void setNegativeFlagFromValue(uint8_t value) {
    if (((value >> 7) & 1U) == 1) { // check if bit 7 is high
        setNegativeFlag();
    } else {
        clearNegativeFlag();
    }
}

void setZeroFlagFromValue(uint8_t value) {
    if (value == 0) { // check if bit 7 is high
        setZeroFlag();
    } else {
        clearZeroFlag();
    }
}

bool isCarryFlagSet() {
    return (((virtualStatus >> 0) & 1U) == 1);
}

bool isZeroFlagSet() {
    return (((virtualStatus >> 1) & 1U) == 1);
}

bool isInterruptDisableSet() {
    return (((virtualStatus >> 2) & 1U) == 1);
}

bool isDecimalModeSet() {
    return (((virtualStatus >> 3) & 1U) == 1);
}

bool isBreakFlagSet() {
    return (((virtualStatus >> 4) & 1U) == 1);
}

bool isOverflowFlagSet() {
    return (((virtualStatus >> 6) & 1U) == 1);
}

bool isNegativeFlagSet() {
    return (((virtualStatus >> 7) & 1U) == 1);
}

// virtualStatus |= 1UL << 4; set bit 4 to 1
// virtualStatus &= ~(1UL << 4); set bit 4 to 0
// virtualStatus ^= 1UL << 4; flip bit 4

void resetProcessor() {
    virtualProgramCounter = getShortFromAddress(0xfffc);
    printf("RESET program counter to to 0x%04x\n",virtualProgramCounter);
    halted = false;
    virtualStackPointer = 0xfd;
}

void printAddress(int s, int e) {
    for (int i = s; i <= e; i++) {
        printf("0x%04x", i * 16);
        std::cout << " ";
        for (int j = 0; j < 16; j++) {
            if ((j % 2) == 0) {
                std::cout << " ";
            }
            printf("%02x", virtualMemoryMap[(i*16) + j]);
        }
        std::cout << "\n";
    }
}

void printAddressAbs(int s, int e) {
    printAddress(s / 16, e / 16);
}

uint16_t addressingModeAbsolute() {
    return getShortFromAddress(virtualProgramCounter + 1);
}

uint16_t addressingModeZeroPage() {
    return virtualMemoryMap[virtualProgramCounter + 1];
}

uint16_t addressingModeRelative() {
    return virtualProgramCounter + static_cast<int8_t>(virtualMemoryMap[virtualProgramCounter + 1]);
}

uint16_t addressingModeAbsoluteIndirect() {
    return getShortFromAddress(virtualProgramCounter + 1);
}

uint16_t addressingModeAbsoluteIndexedWithX() {
    return getShortFromAddress(virtualProgramCounter + 1) + virtualXRegister;
}

uint16_t addressingModeAbsoluteIndexedWithY() {
    return getShortFromAddress(virtualProgramCounter + 1) + virtualYRegister;
}

uint16_t addressingModeZeroPageIndexedWithX() {
    return virtualMemoryMap[virtualProgramCounter + 1] + virtualXRegister;
}

uint16_t addressingModeZeroPageIndexedWithY() {
    return virtualMemoryMap[virtualProgramCounter + 1] + virtualYRegister;
}

uint16_t addressingModeZeroPageIndexedIndirect() {
    return getShortFromAddress(virtualMemoryMap[virtualProgramCounter + 1] + virtualXRegister);
}

uint16_t addressingModeZeroPageIndirectIndexedWithY() {
    return getShortFromAddress(virtualMemoryMap[virtualProgramCounter + 1]) + virtualYRegister;
}

void doCompareSettingFlags(uint8_t registerValue, uint8_t memoryValue) {
    if (registerValue < memoryValue) {
        setNegativeFlag();
        clearZeroFlag();
        clearCarryFlag();
    } else if (registerValue > memoryValue) {
        clearNegativeFlag();
        clearZeroFlag();
        setCarryFlag();
    } else {
        clearNegativeFlag();
        setZeroFlag();
        setCarryFlag();
    }
}

void doBitTest(uint8_t memoryValue) {
    uint8_t andedValue = memoryValue & virtualAccumulator;
    if (andedValue == 0) {
        setZeroFlag();
    } else {
        clearZeroFlag();
    }
    if (((memoryValue >> 7) & 1U) == 1) {
        setNegativeFlag();
    } else {
        clearNegativeFlag();
    }
    if (((memoryValue >> 6) & 1U) == 1) {
        setOverflowFlag();
    } else {
        clearOverflowFlag();
    }
}

void doAdditionSettingFlags(uint8_t valueB) {
    uint8_t valueA = virtualAccumulator;
    virtualAccumulator = valueA + valueB + (uint8_t)isCarryFlagSet();
    setNegativeFlagFromValue(virtualAccumulator);
    setZeroFlagFromValue(virtualAccumulator);
    if ( ( ((valueB >> 7) & 1U) == ((valueA >> 7) & 1U) ) && ( ((virtualAccumulator >> 7) & 1U) != ((valueA >> 7) & 1U) ) ) { // If both parts of the sum have the same sign, but the result does not there has been an overflow
        setOverflowFlag();
    } else {
        clearOverflowFlag();
    }
    if (virtualAccumulator < valueA) {
        setCarryFlag();
    } else {
        clearCarryFlag();
    }
}

void doSubtractionSettingFlags(uint8_t valueB) {
    uint8_t valueA = virtualAccumulator;
    virtualAccumulator = valueA - valueB - (uint8_t)(!isCarryFlagSet());
    setNegativeFlagFromValue(virtualAccumulator);
    setZeroFlagFromValue(virtualAccumulator);
    if ( ( ((valueB >> 7) & 1U) == ((valueA >> 7) & 1U) ) && ( ((virtualAccumulator >> 7) & 1U) != ((valueA >> 7) & 1U) ) ) { // If both parts of the sum have the same sign, but the result does not there has been an overflow
        setOverflowFlag();
    } else {
        clearOverflowFlag();
    }
    if (virtualAccumulator < valueA) {
        setCarryFlag();
    } else {
        clearCarryFlag();
    }
}

bool printInstructions = false;
bool pauseOnError = true;
bool stepThrough = false;
uint32_t stepThroughAt = 0;
bool showDetailedDebugInfo = false;

bool stepVirtualProcessor() {
    bool instructionFailed = false;
    
    uint8_t operandByte = virtualMemoryMap[virtualProgramCounter + 1];

    uint16_t actingAddressShort;
    
    //uint8_t actingValueByte;
    //uint16_t actingValueShort;
    
    const uint8_t bitZeroTest = 1 << 0; // select bit 0
    const uint8_t bitSevenTest = 1 << 7; // select bit 0
    
    //https://en.wikibooks.org/wiki/6502_Assembly#Instructions
    
    /*Instruction progress
     * 
     * #IMPLEMENTED FEATURES
     * Set and Clear Flags
     * Miscellaneous
     * Compare and Test Bit
     * Increment and Decrement
     * Load and Store
     * Shift and Rotate
     * Stack
     * Transfer
     * Arithmetic
     * Logic
     * Subroutines and Jump
     * Branch
     * 
     */
    
    switch (virtualMemoryMap[virtualProgramCounter]) {
        //FLAGS
        case (uint8_t)0x18:
            if (printInstructions) printf("CLC Clear Carry Flag");
            clearCarryFlag();
            virtualProgramCounter++;
            break;
        case (uint8_t)0x38:
            if (printInstructions) printf("SEC Set Carry Flag");
            setCarryFlag();
            virtualProgramCounter++;
            break;
        case (uint8_t)0xd8:
            if (printInstructions) printf("CLD Clear Decimal Mode");
            clearDecimalMode();
            virtualProgramCounter++;
            break;
        case (uint8_t)0xf8:
            if (printInstructions) printf("SED Set Decimal Mode");
            setDecimalMode();
            virtualProgramCounter++;
            break;
        case (uint8_t)0x58:
            if (printInstructions) printf("CLI Clear Interrupt Disable Status");
            clearInterruptDisableStatus();
            virtualProgramCounter++;
            break;
        case (uint8_t)0x78:
            if (printInstructions) printf("SEI Set Interrupt Disable Status");
            setInterruptDisableStatus();
            virtualProgramCounter++;
            break;
        case (uint8_t)0xb8:
            if (printInstructions) printf("CLV Clear Overflow Flag");
            clearOverflowFlag();
            virtualProgramCounter++;
            break;
        //LOGIC
        case (uint8_t)0x2d:
            actingAddressShort = addressingModeAbsolute();
            if (printInstructions) printf("AND data at 0x%04x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            virtualAccumulator = virtualAccumulator & virtualMemoryMap[actingAddressShort];
            setNegativeFlagFromValue(virtualAccumulator);
            setZeroFlagFromValue(virtualAccumulator);
            virtualProgramCounter += 3;
            break;
        case (uint8_t)0x3d:
            actingAddressShort = addressingModeAbsoluteIndexedWithX();
            if (printInstructions) printf("AND data at 0x%04x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            virtualAccumulator = virtualAccumulator & virtualMemoryMap[actingAddressShort];
            setNegativeFlagFromValue(virtualAccumulator);
            setZeroFlagFromValue(virtualAccumulator);
            virtualProgramCounter += 3;
            break;
        case (uint8_t)0x39:
            actingAddressShort = addressingModeAbsoluteIndexedWithY();
            if (printInstructions) printf("AND data at 0x%04x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            virtualAccumulator = virtualAccumulator & virtualMemoryMap[actingAddressShort];
            setNegativeFlagFromValue(virtualAccumulator);
            setZeroFlagFromValue(virtualAccumulator);
            virtualProgramCounter += 3;
            break;
        case (uint8_t)0x29:
            if (printInstructions) printf("AND literal 0x%02x",operandByte);
            virtualAccumulator = virtualAccumulator & operandByte;
            setNegativeFlagFromValue(virtualAccumulator);
            setZeroFlagFromValue(virtualAccumulator);
            virtualProgramCounter += 2;
            break;
        case (uint8_t)0x25:
            actingAddressShort = addressingModeZeroPage();
            if (printInstructions) printf("AND data at 0x%04x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            virtualAccumulator = virtualAccumulator & virtualMemoryMap[actingAddressShort];
            setNegativeFlagFromValue(virtualAccumulator);
            setZeroFlagFromValue(virtualAccumulator);
            virtualProgramCounter += 2;
            break;
        case (uint8_t)0x21:
            actingAddressShort = addressingModeZeroPageIndexedIndirect();
            if (printInstructions) printf("AND data at 0x%04x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            virtualAccumulator = virtualAccumulator & virtualMemoryMap[actingAddressShort];
            setNegativeFlagFromValue(virtualAccumulator);
            setZeroFlagFromValue(virtualAccumulator);
            virtualProgramCounter += 2;
            break;
        case (uint8_t)0x35:
            actingAddressShort = addressingModeZeroPageIndexedWithX();
            if (printInstructions) printf("AND data at 0x%04x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            virtualAccumulator = virtualAccumulator & virtualMemoryMap[actingAddressShort];
            setNegativeFlagFromValue(virtualAccumulator);
            setZeroFlagFromValue(virtualAccumulator);
            virtualProgramCounter += 2;
            break;
        case (uint8_t)0x31:
            actingAddressShort = addressingModeZeroPageIndirectIndexedWithY();
            if (printInstructions) printf("AND data at 0x%04x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            virtualAccumulator = virtualAccumulator & virtualMemoryMap[actingAddressShort];
            setNegativeFlagFromValue(virtualAccumulator);
            setZeroFlagFromValue(virtualAccumulator);
            virtualProgramCounter += 2;
            break;
            
        case (uint8_t)0x0d:
            actingAddressShort = addressingModeAbsolute();
            if (printInstructions) printf("ORA data at 0x%04x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            virtualAccumulator = virtualAccumulator | virtualMemoryMap[actingAddressShort];
            setNegativeFlagFromValue(virtualAccumulator);
            setZeroFlagFromValue(virtualAccumulator);
            virtualProgramCounter += 3;
            break;
        case (uint8_t)0x1d:
            actingAddressShort = addressingModeAbsoluteIndexedWithX();
            if (printInstructions) printf("ORA data at 0x%04x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            virtualAccumulator = virtualAccumulator | virtualMemoryMap[actingAddressShort];
            setNegativeFlagFromValue(virtualAccumulator);
            setZeroFlagFromValue(virtualAccumulator);
            virtualProgramCounter += 3;
            break;
        case (uint8_t)0x19:
            actingAddressShort = addressingModeAbsoluteIndexedWithY();
            if (printInstructions) printf("ORA data at 0x%04x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            virtualAccumulator = virtualAccumulator | virtualMemoryMap[actingAddressShort];
            setNegativeFlagFromValue(virtualAccumulator);
            setZeroFlagFromValue(virtualAccumulator);
            virtualProgramCounter += 3;
            break;
        case (uint8_t)0x09:
            if (printInstructions) printf("ORA literal 0x%02x",operandByte);
            virtualAccumulator = virtualAccumulator | operandByte;
            setNegativeFlagFromValue(virtualAccumulator);
            setZeroFlagFromValue(virtualAccumulator);
            virtualProgramCounter += 2;
            break;
        case (uint8_t)0x05:
            actingAddressShort = addressingModeZeroPage();
            if (printInstructions) printf("ORA data at 0x%04x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            virtualAccumulator = virtualAccumulator | virtualMemoryMap[actingAddressShort];
            setNegativeFlagFromValue(virtualAccumulator);
            setZeroFlagFromValue(virtualAccumulator);
            virtualProgramCounter += 2;
            break;
        case (uint8_t)0x01:
            actingAddressShort = addressingModeZeroPageIndexedIndirect();
            if (printInstructions) printf("ORA data at 0x%04x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            virtualAccumulator = virtualAccumulator | virtualMemoryMap[actingAddressShort];
            setNegativeFlagFromValue(virtualAccumulator);
            setZeroFlagFromValue(virtualAccumulator);
            virtualProgramCounter += 2;
            break;
        case (uint8_t)0x15:
            actingAddressShort = addressingModeZeroPageIndexedWithX();
            if (printInstructions) printf("ORA data at 0x%04x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            virtualAccumulator = virtualAccumulator | virtualMemoryMap[actingAddressShort];
            setNegativeFlagFromValue(virtualAccumulator);
            setZeroFlagFromValue(virtualAccumulator);
            virtualProgramCounter += 2;
            break;
        case (uint8_t)0x11:
            actingAddressShort = addressingModeZeroPageIndirectIndexedWithY();
            if (printInstructions) printf("ORA data at 0x%04x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            virtualAccumulator = virtualAccumulator | virtualMemoryMap[actingAddressShort];
            setNegativeFlagFromValue(virtualAccumulator);
            setZeroFlagFromValue(virtualAccumulator);
            virtualProgramCounter += 2;
            break;
            
        case (uint8_t)0x4d:
            actingAddressShort = addressingModeAbsolute();
            if (printInstructions) printf("EOR data at 0x%04x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            virtualAccumulator = virtualAccumulator ^ virtualMemoryMap[actingAddressShort];
            setNegativeFlagFromValue(virtualAccumulator);
            setZeroFlagFromValue(virtualAccumulator);
            virtualProgramCounter += 3;
            break;
        case (uint8_t)0x5d:
            actingAddressShort = addressingModeAbsoluteIndexedWithX();
            if (printInstructions) printf("EOR data at 0x%04x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            virtualAccumulator = virtualAccumulator ^ virtualMemoryMap[actingAddressShort];
            setNegativeFlagFromValue(virtualAccumulator);
            setZeroFlagFromValue(virtualAccumulator);
            virtualProgramCounter += 3;
            break;
        case (uint8_t)0x59:
            actingAddressShort = addressingModeAbsoluteIndexedWithY();
            if (printInstructions) printf("EOR data at 0x%04x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            virtualAccumulator = virtualAccumulator ^ virtualMemoryMap[actingAddressShort];
            setNegativeFlagFromValue(virtualAccumulator);
            setZeroFlagFromValue(virtualAccumulator);
            virtualProgramCounter += 3;
            break;
        case (uint8_t)0x49:
            if (printInstructions) printf("EOR literal 0x%02x",operandByte);
            virtualAccumulator = virtualAccumulator ^ operandByte;
            setNegativeFlagFromValue(virtualAccumulator);
            setZeroFlagFromValue(virtualAccumulator);
            virtualProgramCounter += 2;
            break;
        case (uint8_t)0x45:
            actingAddressShort = addressingModeZeroPage();
            if (printInstructions) printf("EOR data at 0x%04x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            virtualAccumulator = virtualAccumulator ^ virtualMemoryMap[actingAddressShort];
            setNegativeFlagFromValue(virtualAccumulator);
            setZeroFlagFromValue(virtualAccumulator);
            virtualProgramCounter += 2;
            break;
        case (uint8_t)0x41:
            actingAddressShort = addressingModeZeroPageIndexedIndirect();
            if (printInstructions) printf("EOR data at 0x%04x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            virtualAccumulator = virtualAccumulator ^ virtualMemoryMap[actingAddressShort];
            setNegativeFlagFromValue(virtualAccumulator);
            setZeroFlagFromValue(virtualAccumulator);
            virtualProgramCounter += 2;
            break;
        case (uint8_t)0x55:
            actingAddressShort = addressingModeZeroPageIndexedWithX();
            if (printInstructions) printf("EOR data at 0x%04x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            virtualAccumulator = virtualAccumulator ^ virtualMemoryMap[actingAddressShort];
            setNegativeFlagFromValue(virtualAccumulator);
            setZeroFlagFromValue(virtualAccumulator);
            virtualProgramCounter += 2;
            break;
        case (uint8_t)0x51:
            actingAddressShort = addressingModeZeroPageIndirectIndexedWithY();
            if (printInstructions) printf("EOR data at 0x%04x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            virtualAccumulator = virtualAccumulator ^ virtualMemoryMap[actingAddressShort];
            setNegativeFlagFromValue(virtualAccumulator);
            setZeroFlagFromValue(virtualAccumulator);
            virtualProgramCounter += 2;
            break;
        //INCREMENT AND DECREMENT
        case (uint8_t)0xee:
            actingAddressShort = addressingModeAbsolute();
            if (printInstructions) printf("INC data at 0x%04x (existing value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            virtualMemoryMap[actingAddressShort]++;
            setNegativeFlagFromValue(virtualMemoryMap[actingAddressShort]);
            setZeroFlagFromValue(virtualMemoryMap[actingAddressShort]);
            virtualProgramCounter += 3;
            break;
        case (uint8_t)0xfe:
            actingAddressShort = addressingModeAbsoluteIndexedWithX();
            if (printInstructions) printf("INC data at 0x%04x (existing value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            virtualMemoryMap[actingAddressShort]++;
            setNegativeFlagFromValue(virtualMemoryMap[actingAddressShort]);
            setZeroFlagFromValue(virtualMemoryMap[actingAddressShort]);
            virtualProgramCounter += 3;
            break;
        case (uint8_t)0xe6:
            actingAddressShort = addressingModeZeroPage();
            if (printInstructions) printf("INC data at 0x%04x (existing value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            virtualMemoryMap[actingAddressShort]++;
            setNegativeFlagFromValue(virtualMemoryMap[actingAddressShort]);
            setZeroFlagFromValue(virtualMemoryMap[actingAddressShort]);
            virtualProgramCounter += 2;
            break;
        case (uint8_t)0xf6:
            actingAddressShort = addressingModeZeroPageIndexedWithX();
            if (printInstructions) printf("INC data at 0x%04x (existing value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            virtualMemoryMap[actingAddressShort]++;
            setNegativeFlagFromValue(virtualMemoryMap[actingAddressShort]);
            setZeroFlagFromValue(virtualMemoryMap[actingAddressShort]);
            virtualProgramCounter += 2;
            break;
            
        case (uint8_t)0xe8:
            if (printInstructions) printf("INX existing value 0x%02x",virtualXRegister);
            virtualXRegister++;
            setNegativeFlagFromValue(virtualXRegister);
            setZeroFlagFromValue(virtualXRegister);
            virtualProgramCounter++;
            break;
        case (uint8_t)0xc8:
            if (printInstructions) printf("INY existing value 0x%02x",virtualYRegister);
            virtualYRegister++;
            setNegativeFlagFromValue(virtualYRegister);
            setZeroFlagFromValue(virtualYRegister);
            virtualProgramCounter++;
            break;
            
        case (uint8_t)0xce:
            actingAddressShort = addressingModeAbsolute();
            if (printInstructions) printf("DEC data at 0x%04x (existing value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            virtualMemoryMap[actingAddressShort]--;
            setNegativeFlagFromValue(virtualMemoryMap[actingAddressShort]);
            setZeroFlagFromValue(virtualMemoryMap[actingAddressShort]);
            virtualProgramCounter += 3;
            break;
        case (uint8_t)0xde:
            actingAddressShort = addressingModeAbsoluteIndexedWithX();
            if (printInstructions) printf("DEC data at 0x%04x (existing value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            virtualMemoryMap[actingAddressShort]--;
            setNegativeFlagFromValue(virtualMemoryMap[actingAddressShort]);
            setZeroFlagFromValue(virtualMemoryMap[actingAddressShort]);
            virtualProgramCounter += 3;
            break;
        case (uint8_t)0xc6:
            actingAddressShort = addressingModeZeroPage();
            if (printInstructions) printf("DEC data at 0x%04x (existing value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            virtualMemoryMap[actingAddressShort]--;
            setNegativeFlagFromValue(virtualMemoryMap[actingAddressShort]);
            setZeroFlagFromValue(virtualMemoryMap[actingAddressShort]);
            virtualProgramCounter += 2;
            break;
        case (uint8_t)0xd6:
            actingAddressShort = addressingModeZeroPageIndexedWithX();
            if (printInstructions) printf("DEC data at 0x%04x (existing value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            virtualMemoryMap[actingAddressShort]--;
            setNegativeFlagFromValue(virtualMemoryMap[actingAddressShort]);
            setZeroFlagFromValue(virtualMemoryMap[actingAddressShort]);
            virtualProgramCounter += 2;
            break;
            
        case (uint8_t)0xca:
            if (printInstructions) printf("DEX existing value 0x%02x",virtualXRegister);
            virtualXRegister--;
            setNegativeFlagFromValue(virtualXRegister);
            setZeroFlagFromValue(virtualXRegister);
            virtualProgramCounter++;
            break;
        case (uint8_t)0x88:
            if (printInstructions) printf("DEY existing value 0x%02x",virtualYRegister);
            virtualYRegister--;
            setNegativeFlagFromValue(virtualYRegister);
            setZeroFlagFromValue(virtualYRegister);
            virtualProgramCounter++;
            break;
        //ARITHMETIC
        case (uint8_t)0x6d:
            actingAddressShort = addressingModeAbsolute();
            if (printInstructions) printf("ADC with data at 0x%04x ",actingAddressShort);
            doAdditionSettingFlags(virtualMemoryMap[actingAddressShort]);
            virtualProgramCounter += 3;
            break;
        case (uint8_t)0x7d:
            actingAddressShort = addressingModeAbsoluteIndexedWithX();
            if (printInstructions) printf("ADC with data at 0x%04x ",actingAddressShort);
            doAdditionSettingFlags(virtualMemoryMap[actingAddressShort]);
            virtualProgramCounter += 3;
            break;
        case (uint8_t)0x79:
            actingAddressShort = addressingModeAbsoluteIndexedWithY();
            if (printInstructions) printf("ADC with data at 0x%04x ",actingAddressShort);
            doAdditionSettingFlags(virtualMemoryMap[actingAddressShort]);
            virtualProgramCounter += 3;
            break;
        case (uint8_t)0x69:
            if (printInstructions) printf("ADC with literal 0x%02x ",operandByte);
            doAdditionSettingFlags(operandByte);
            virtualProgramCounter += 2;
            break;
        case (uint8_t)0x65:
            actingAddressShort = addressingModeZeroPage();
            if (printInstructions) printf("ADC with data at 0x%04x ",actingAddressShort);
            doAdditionSettingFlags(virtualMemoryMap[actingAddressShort]);
            virtualProgramCounter += 2;
            break;
        case (uint8_t)0x61:
            actingAddressShort = addressingModeZeroPageIndexedIndirect();
            if (printInstructions) printf("ADC with data at 0x%04x ",actingAddressShort);
            doAdditionSettingFlags(virtualMemoryMap[actingAddressShort]);
            virtualProgramCounter += 2;
            break;
        case (uint8_t)0x75:
            actingAddressShort = addressingModeZeroPageIndexedWithX();
            if (printInstructions) printf("ADC with data at 0x%04x ",actingAddressShort);
            doAdditionSettingFlags(virtualMemoryMap[actingAddressShort]);
            virtualProgramCounter += 2;
            break;
        case (uint8_t)0x71:
            actingAddressShort = addressingModeZeroPageIndirectIndexedWithY();
            if (printInstructions) printf("ADC with data at 0x%04x ",actingAddressShort);
            doAdditionSettingFlags(virtualMemoryMap[actingAddressShort]);
            virtualProgramCounter += 2;
            break;
            
        case (uint8_t)0xed:
            actingAddressShort = addressingModeAbsolute();
            if (printInstructions) printf("SBC with data at 0x%04x ",actingAddressShort);
            doSubtractionSettingFlags(virtualMemoryMap[actingAddressShort]);
            virtualProgramCounter += 3;
            break;
        case (uint8_t)0xfd:
            actingAddressShort = addressingModeAbsoluteIndexedWithX();
            if (printInstructions) printf("SBC with data at 0x%04x ",actingAddressShort);
            doSubtractionSettingFlags(virtualMemoryMap[actingAddressShort]);
            virtualProgramCounter += 3;
            break;
        case (uint8_t)0xf9:
            actingAddressShort = addressingModeAbsoluteIndexedWithY();
            if (printInstructions) printf("SBC with data at 0x%04x ",actingAddressShort);
            doSubtractionSettingFlags(virtualMemoryMap[actingAddressShort]);
            virtualProgramCounter += 3;
            break;
        case (uint8_t)0xe9:
            if (printInstructions) printf("SBC with literal 0x%02x ",operandByte);
            doSubtractionSettingFlags(operandByte);
            virtualProgramCounter += 2;
            break;
        case (uint8_t)0xe5:
            actingAddressShort = addressingModeZeroPage();
            if (printInstructions) printf("SBC with data at 0x%04x ",actingAddressShort);
            doSubtractionSettingFlags(virtualMemoryMap[actingAddressShort]);
            virtualProgramCounter += 2;
            break;
        case (uint8_t)0xe1:
            actingAddressShort = addressingModeZeroPageIndexedIndirect();
            if (printInstructions) printf("SBC with data at 0x%04x ",actingAddressShort);
            doSubtractionSettingFlags(virtualMemoryMap[actingAddressShort]);
            virtualProgramCounter += 2;
            break;
        case (uint8_t)0xf5:
            actingAddressShort = addressingModeZeroPageIndexedWithX();
            if (printInstructions) printf("SBC with data at 0x%04x ",actingAddressShort);
            doSubtractionSettingFlags(virtualMemoryMap[actingAddressShort]);
            virtualProgramCounter += 2;
            break;
        case (uint8_t)0xf1:
            actingAddressShort = addressingModeZeroPageIndirectIndexedWithY();
            if (printInstructions) printf("SBC with data at 0x%04x ",actingAddressShort);
            doSubtractionSettingFlags(virtualMemoryMap[actingAddressShort]);
            virtualProgramCounter += 2;
            break;
        //COMPARE AND TEST BIT
            //CMP
        case (uint8_t)0xcd:
            actingAddressShort = addressingModeAbsolute();
            if (printInstructions) printf("CMP with data at 0x%04x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            doCompareSettingFlags(virtualAccumulator,virtualMemoryMap[actingAddressShort]);
            virtualProgramCounter += 3;
            break;
        case (uint8_t)0xdd:
            actingAddressShort = addressingModeAbsoluteIndexedWithX();
            if (printInstructions) printf("CMP with data at 0x%04x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            doCompareSettingFlags(virtualAccumulator,virtualMemoryMap[actingAddressShort]);
            virtualProgramCounter += 3;
            break;
        case (uint8_t)0xd9:
            actingAddressShort = addressingModeAbsoluteIndexedWithY();
            if (printInstructions) printf("CMP with data at 0x%04x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            doCompareSettingFlags(virtualAccumulator,virtualMemoryMap[actingAddressShort]);
            virtualProgramCounter += 3;
            break;
        case (uint8_t)0xc9:
            if (printInstructions) printf("CMP with operand (value 0x%02x)",operandByte);
            doCompareSettingFlags(virtualAccumulator,operandByte);
            virtualProgramCounter += 2;
            break;
        case (uint8_t)0xc5:
            actingAddressShort = addressingModeZeroPage();
            if (printInstructions) printf("CMP with data at 0x%04x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            doCompareSettingFlags(virtualAccumulator,virtualMemoryMap[actingAddressShort]);
            virtualProgramCounter += 3;
            break;
        case (uint8_t)0xc1:
            actingAddressShort = addressingModeZeroPageIndexedIndirect();
            if (printInstructions) printf("CMP with data at 0x%04x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            doCompareSettingFlags(virtualAccumulator,virtualMemoryMap[actingAddressShort]);
            virtualProgramCounter += 3;
            break;
        case (uint8_t)0xd5:
            actingAddressShort = addressingModeZeroPageIndexedWithX();
            if (printInstructions) printf("CMP with data at 0x%04x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            doCompareSettingFlags(virtualAccumulator,virtualMemoryMap[actingAddressShort]);
            virtualProgramCounter += 3;
            break;
        case (uint8_t)0xd1:
            actingAddressShort = addressingModeZeroPageIndirectIndexedWithY();
            if (printInstructions) printf("CMP with data at 0x%04x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            doCompareSettingFlags(virtualAccumulator,virtualMemoryMap[actingAddressShort]);
            virtualProgramCounter += 3;
            break;
            //CPX
        case (uint8_t)0xec:
            actingAddressShort = addressingModeAbsolute();
            if (printInstructions) printf("CPX with data at 0x%04x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            doCompareSettingFlags(virtualXRegister,virtualMemoryMap[actingAddressShort]);
            virtualProgramCounter += 3;
            break;
        case (uint8_t)0xe0:
            if (printInstructions) printf("CPX with operand (value 0x%02x)",operandByte);
            doCompareSettingFlags(virtualXRegister,operandByte);
            virtualProgramCounter += 2;
            break;
        case (uint8_t)0xe4:
            actingAddressShort = addressingModeZeroPage();
            if (printInstructions) printf("CPX with data at 0x%04x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            doCompareSettingFlags(virtualXRegister,virtualMemoryMap[actingAddressShort]);
            virtualProgramCounter += 3;
            break;
            //CPY
        case (uint8_t)0xcc:
            actingAddressShort = addressingModeAbsolute();
            if (printInstructions) printf("CPY with data at 0x%04x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            doCompareSettingFlags(virtualYRegister,virtualMemoryMap[actingAddressShort]);
            virtualProgramCounter += 3;
            break;
        case (uint8_t)0xc0:
            if (printInstructions) printf("CPY with operand (value 0x%02x)",operandByte);
            doCompareSettingFlags(virtualYRegister,operandByte);
            virtualProgramCounter += 2;
            break;
        case (uint8_t)0xc4:
            actingAddressShort = addressingModeZeroPage();
            if (printInstructions) printf("CPY with data at 0x%04x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            doCompareSettingFlags(virtualYRegister,virtualMemoryMap[actingAddressShort]);
            virtualProgramCounter += 3;
            break;
            
        case (uint8_t)0x2c:
            actingAddressShort = addressingModeAbsolute();
            if (printInstructions) printf("BIT with data at 0x%04x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            doBitTest(virtualMemoryMap[actingAddressShort]);
            virtualProgramCounter += 3;
            break;
        case (uint8_t)0x89:
            if (printInstructions) printf("BIT with operand (value 0x%02x)",operandByte);
            doBitTest(operandByte);
            virtualProgramCounter += 2;
            break;
        case (uint8_t)0x24:
            actingAddressShort = addressingModeZeroPage();
            if (printInstructions) printf("BIT with data at 0x%04x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            doBitTest(virtualMemoryMap[actingAddressShort]);
            virtualProgramCounter += 3;
            break;
        //TRANSFERS
        case (uint8_t)0xaa:
            if (printInstructions) printf("TAX");
            virtualXRegister = virtualAccumulator;
            setNegativeFlagFromValue(virtualAccumulator);
            setZeroFlagFromValue(virtualAccumulator);
            virtualProgramCounter++;
            break;
        case (uint8_t)0x8a:
            if (printInstructions) printf("TXA");
            virtualAccumulator = virtualXRegister;
            setNegativeFlagFromValue(virtualAccumulator);
            setZeroFlagFromValue(virtualAccumulator);
            virtualProgramCounter++;
            break;
        case (uint8_t)0xa8:
            if (printInstructions) printf("TAY");
            virtualYRegister = virtualAccumulator;
            setNegativeFlagFromValue(virtualAccumulator);
            setZeroFlagFromValue(virtualAccumulator);
            virtualProgramCounter++;
            break;
        case (uint8_t)0x98:
            if (printInstructions) printf("TYA");
            virtualAccumulator = virtualYRegister;
            setNegativeFlagFromValue(virtualAccumulator);
            setZeroFlagFromValue(virtualAccumulator);
            virtualProgramCounter++;
            break;
        case (uint8_t)0xba:
            if (printInstructions) printf("TSX");
            virtualXRegister = virtualStackPointer;
            setNegativeFlagFromValue(virtualStackPointer);
            setZeroFlagFromValue(virtualStackPointer);
            virtualProgramCounter++;
            break;
        case (uint8_t)0x9a:
            if (printInstructions) printf("TXS");
            virtualStackPointer = virtualXRegister;
            virtualProgramCounter++;
            break;
        //STACK
        case (uint8_t)0x48:
            if (printInstructions) printf("PHA Push Accumulator on Stack");
            //virtualAccumulator = virtualXRegister;
            pushByteToStack(virtualAccumulator);
            if (printInstructions) printf("\nStack Status:\n");
            if (showDetailedDebugInfo) printAddressAbs(0x0100, 0x01ff);
            virtualProgramCounter++;
            break;
        case (uint8_t)0x08:
            if (printInstructions) printf("PHP Push Processor Status on Stack");
            //virtualAccumulator = virtualXRegister;
            pushByteToStack(virtualStatus);
            if (printInstructions) printf("\nStack Status:\n");
            if (showDetailedDebugInfo) printAddressAbs(0x0100, 0x01ff);
            virtualProgramCounter++;
            break;
        case (uint8_t)0x68:
            if (printInstructions) printf("PLA Pull Accumulator from Stack");
            //virtualAccumulator = virtualXRegister;
            virtualAccumulator = popByteFromStack();
            setNegativeFlagFromValue(virtualAccumulator);
            setZeroFlagFromValue(virtualAccumulator);
            if (printInstructions) printf("\nStack Status:\n");
            if (showDetailedDebugInfo) printAddressAbs(0x0100, 0x01ff);
            virtualProgramCounter++;
            break;
        case (uint8_t)0x28:
            if (printInstructions) printf("PLP Pull Processor Status from Stack");
            //virtualAccumulator = virtualXRegister;
            virtualStatus = popByteFromStack();
            if (printInstructions) printf("\nStack Status:\n");
            if (showDetailedDebugInfo) printAddressAbs(0x0100, 0x01ff);
            virtualProgramCounter++;
            break;
        //JUMPS
        case (uint8_t)0x4c:
            virtualProgramCounter = getShortFromAddress(virtualProgramCounter + 1);
            if (printInstructions) printf("JMP Absolute to 0x%04x",virtualProgramCounter);
            break;
        case (uint8_t)0x6c:
            virtualProgramCounter = getShortFromAddress(getShortFromAddress(virtualProgramCounter + 1));
            if (printInstructions) printf("JMP Indirect to 0x%04x",virtualProgramCounter);
            break;
        case (uint8_t)0x20:
            if (printInstructions) printf("JSR Absolute to 0x%04x",getShortFromAddress(virtualProgramCounter + 1));
            pushShortToStack(virtualProgramCounter + 2);// push address of next instruction less one to stack
            if (printInstructions) printf("\nStack Status:\n");
            if (showDetailedDebugInfo) printAddressAbs(0x0100, 0x01ff);
            virtualProgramCounter = getShortFromAddress(virtualProgramCounter + 1);
            break;
        case (uint8_t)0x60:
            virtualProgramCounter = popShortFromStack();
            virtualProgramCounter++;
            if (printInstructions) printf("\nStack Status:\n");
            if (showDetailedDebugInfo) printAddressAbs(0x0100, 0x01ff);
            if (printInstructions) printf("RTS to 0x%04x",virtualProgramCounter);
            break;
        case (uint8_t)0x40:
            virtualStatus = popByteFromStack();
            virtualProgramCounter = popShortFromStack();
            virtualProgramCounter++;
            if (printInstructions) printf("\nStack Status:\n");
            if (showDetailedDebugInfo) printAddressAbs(0x0100, 0x01ff);
            if (printInstructions) printf("RTI to 0x%04x with status 0x%02x",virtualProgramCounter, virtualStatus);
            break;
        //BRANCH
        case (uint8_t)0x90:
            if (printInstructions) printf("BCC Branch on Carry Clear to 0x%04x (displacement 0x%02x)",virtualProgramCounter + static_cast<int8_t>(operandByte) + 2, static_cast<int8_t>(operandByte));
            if (!isCarryFlagSet()) {
                if (printInstructions) printf(" - Did Jump");
                virtualProgramCounter = virtualProgramCounter + static_cast<int8_t>(operandByte) + 2;
            } else {
                if (printInstructions) printf(" - Did Not Jump");
                virtualProgramCounter += 2;
            }
            break;
        case (uint8_t)0xb0:
            if (printInstructions) printf("BCS Branch on Carry Set to 0x%04x (displacement 0x%02x)",virtualProgramCounter + static_cast<int8_t>(operandByte) + 2, static_cast<int8_t>(operandByte));
            if (isCarryFlagSet()) {
                if (printInstructions) printf(" - Did Jump");
                virtualProgramCounter = virtualProgramCounter + static_cast<int8_t>(operandByte) + 2;
            } else {
                if (printInstructions) printf(" - Did Not Jump");
                virtualProgramCounter += 2;
            }
            break;
        case (uint8_t)0xd0:
            if (printInstructions) printf("BNE Branch on Result not Zero to 0x%04x (displacement 0x%02x)",virtualProgramCounter + static_cast<int8_t>(operandByte) + 2, static_cast<int8_t>(operandByte));
            if (!isZeroFlagSet()) {
                if (printInstructions) printf(" - Did Jump");
                virtualProgramCounter = virtualProgramCounter + static_cast<int8_t>(operandByte) + 2;
            } else {
                if (printInstructions) printf(" - Did Not Jump");
                virtualProgramCounter += 2;
            }
            break;
        case (uint8_t)0xf0:
            if (printInstructions) printf("BEQ Branch on Result Zero to 0x%04x (displacement 0x%02x)",virtualProgramCounter + static_cast<int8_t>(operandByte) + 2, static_cast<int8_t>(operandByte));
            if (isZeroFlagSet()) {
                if (printInstructions) printf(" - Did Jump");
                virtualProgramCounter = virtualProgramCounter + static_cast<int8_t>(operandByte) + 2;
            } else {
                if (printInstructions) printf(" - Did Not Jump");
                virtualProgramCounter += 2;
            }
            break;
        case (uint8_t)0x10:
            if (printInstructions) printf("BPL Branch on plus to 0x%04x (displacement 0x%02x)",virtualProgramCounter + static_cast<int8_t>(operandByte) + 2, static_cast<int8_t>(operandByte));
            if (!isNegativeFlagSet()) {
                if (printInstructions) printf(" - Did Jump");
                virtualProgramCounter = virtualProgramCounter + static_cast<int8_t>(operandByte) + 2;
            } else {
                if (printInstructions) printf(" - Did Not Jump");
                virtualProgramCounter += 2;
            }
            break;
        case (uint8_t)0x30:
            if (printInstructions) printf("BMI Branch on minus to 0x%04x (displacement 0x%02x)",virtualProgramCounter + static_cast<int8_t>(operandByte) + 2, static_cast<int8_t>(operandByte));
            if (isNegativeFlagSet()) {
                if (printInstructions) printf(" - Did Jump");
                virtualProgramCounter = virtualProgramCounter + static_cast<int8_t>(operandByte) + 2;
            } else {
                if (printInstructions) printf(" - Did Not Jump");
                virtualProgramCounter += 2;
            }
            break;
        case (uint8_t)0x50:
            if (printInstructions) printf("BVC Branch on Overflow Clear to 0x%04x (displacement 0x%02x)",virtualProgramCounter + static_cast<int8_t>(operandByte) + 2, static_cast<int8_t>(operandByte));
            if (!isOverflowFlagSet()) {
                if (printInstructions) printf(" - Did Jump");
                virtualProgramCounter = virtualProgramCounter + static_cast<int8_t>(operandByte) + 2;
            } else {
                if (printInstructions) printf(" - Did Not Jump");
                virtualProgramCounter += 2;
            }
            break;
        case (uint8_t)0x70:
            if (printInstructions) printf("BVS Branch on Overflow Set to 0x%04x (displacement 0x%02x)",virtualProgramCounter + static_cast<int8_t>(operandByte) + 2, static_cast<int8_t>(operandByte));
            if (isOverflowFlagSet()) {
                if (printInstructions) printf(" - Did Jump");
                virtualProgramCounter = virtualProgramCounter + static_cast<int8_t>(operandByte) + 2;
            } else {
                if (printInstructions) printf(" - Did Not Jump");
                virtualProgramCounter += 2;
            }
            break;
        //LOAD
            //LOAD ACC
        case (uint8_t)0xad:
            actingAddressShort = addressingModeAbsolute();
            if (printInstructions) printf("LDA with data at 0x%04x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            virtualAccumulator = virtualMemoryMap[actingAddressShort];
            setNegativeFlagFromValue(virtualAccumulator);
            setZeroFlagFromValue(virtualAccumulator);
            virtualProgramCounter += 3;
            break;
        case (uint8_t)0xbd:
            actingAddressShort = addressingModeAbsoluteIndexedWithX();
            if (printInstructions) printf("LDA with data at 0x%04x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            virtualAccumulator = virtualMemoryMap[actingAddressShort];
            setNegativeFlagFromValue(virtualAccumulator);
            setZeroFlagFromValue(virtualAccumulator);
            virtualProgramCounter += 3;
            break;
        case (uint8_t)0xb9:
            actingAddressShort = addressingModeAbsoluteIndexedWithY();
            if (printInstructions) printf("LDA with data at 0x%04x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            virtualAccumulator = virtualMemoryMap[actingAddressShort];
            setNegativeFlagFromValue(virtualAccumulator);
            setZeroFlagFromValue(virtualAccumulator);
            virtualProgramCounter += 3;
            break;
        case (uint8_t)0xa9:
            if (printInstructions) printf("LDA with 0x%02x literal",operandByte);
            virtualAccumulator = operandByte;
            setNegativeFlagFromValue(virtualAccumulator);
            setZeroFlagFromValue(virtualAccumulator);
            virtualProgramCounter += 2;
            break;
        case (uint8_t)0xa5:
            actingAddressShort = addressingModeZeroPage();
            if (printInstructions) printf("LDA with data at 0x%02x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            virtualAccumulator = virtualMemoryMap[actingAddressShort];
            setNegativeFlagFromValue(virtualAccumulator);
            setZeroFlagFromValue(virtualAccumulator);
            virtualProgramCounter += 2;
            break;
        case (uint8_t)0xa1:
            actingAddressShort = addressingModeZeroPageIndexedIndirect();
            if (printInstructions) printf("LDA with data at 0x%02x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            virtualAccumulator = virtualMemoryMap[actingAddressShort];
            setNegativeFlagFromValue(virtualAccumulator);
            setZeroFlagFromValue(virtualAccumulator);
            virtualProgramCounter += 2;
            break;
        case (uint8_t)0xb5:
            actingAddressShort = addressingModeZeroPageIndexedWithX();
            if (printInstructions) printf("LDA with data at 0x%02x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            virtualAccumulator = virtualMemoryMap[actingAddressShort];
            setNegativeFlagFromValue(virtualAccumulator);
            setZeroFlagFromValue(virtualAccumulator);
            virtualProgramCounter += 2;
            break;
        case (uint8_t)0xb1:
            actingAddressShort = addressingModeZeroPageIndirectIndexedWithY();
            if (printInstructions) printf("LDA with data at 0x%02x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            virtualAccumulator = virtualMemoryMap[actingAddressShort];
            setNegativeFlagFromValue(virtualAccumulator);
            setZeroFlagFromValue(virtualAccumulator);
            virtualProgramCounter += 2;
            break;
            //LOAD X
        case (uint8_t)0xae:
            actingAddressShort = addressingModeAbsolute();
            if (printInstructions) printf("LDX with data at 0x%04x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            virtualXRegister = virtualMemoryMap[actingAddressShort];
            setNegativeFlagFromValue(virtualXRegister);
            setZeroFlagFromValue(virtualXRegister);
            virtualProgramCounter += 3;
            break;
        case (uint8_t)0xbe:
            actingAddressShort = addressingModeAbsoluteIndexedWithY();
            if (printInstructions) printf("LDX with data at 0x%04x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            virtualXRegister = virtualMemoryMap[actingAddressShort];
            setNegativeFlagFromValue(virtualXRegister);
            setZeroFlagFromValue(virtualXRegister);
            virtualProgramCounter += 3;
            break;
        case (uint8_t)0xa2:
            if (printInstructions) printf("LDX with 0x%02x literal",operandByte);
            virtualXRegister = operandByte;
            setNegativeFlagFromValue(virtualXRegister);
            setZeroFlagFromValue(virtualXRegister);
            virtualProgramCounter += 2;
            break;
        case (uint8_t)0xa6:
            actingAddressShort = addressingModeZeroPage();
            if (printInstructions) printf("LDX with data at 0x%02x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            virtualXRegister = virtualMemoryMap[actingAddressShort];
            setNegativeFlagFromValue(virtualXRegister);
            setZeroFlagFromValue(virtualXRegister);
            virtualProgramCounter += 2;
            break;
        case (uint8_t)0xb6:
            actingAddressShort = addressingModeZeroPageIndexedWithY();
            if (printInstructions) printf("LDX with data at 0x%02x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            virtualXRegister = virtualMemoryMap[actingAddressShort];
            setNegativeFlagFromValue(virtualXRegister);
            setZeroFlagFromValue(virtualXRegister);
            virtualProgramCounter += 2;
            break;
            //LOAD Y
        case (uint8_t)0xac:
            actingAddressShort = addressingModeAbsolute();
            if (printInstructions) printf("LDY with data at 0x%04x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            virtualYRegister = virtualMemoryMap[actingAddressShort];
            setNegativeFlagFromValue(virtualYRegister);
            setZeroFlagFromValue(virtualYRegister);
            virtualProgramCounter += 3;
            break;
        case (uint8_t)0xbc:
            actingAddressShort = addressingModeAbsoluteIndexedWithX();
            if (printInstructions) printf("LDY with data at 0x%04x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            virtualYRegister = virtualMemoryMap[actingAddressShort];
            setNegativeFlagFromValue(virtualYRegister);
            setZeroFlagFromValue(virtualYRegister);
            virtualProgramCounter += 3;
            break;
        case (uint8_t)0xa0:
            if (printInstructions) printf("LDY with 0x%02x literal",operandByte);
            virtualYRegister = operandByte;
            setNegativeFlagFromValue(virtualYRegister);
            setZeroFlagFromValue(virtualYRegister);
            virtualProgramCounter += 2;
            break;
        case (uint8_t)0xa4:
            actingAddressShort = addressingModeZeroPage();
            if (printInstructions) printf("LDY with data at 0x%02x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            virtualYRegister = virtualMemoryMap[actingAddressShort];
            setNegativeFlagFromValue(virtualYRegister);
            setZeroFlagFromValue(virtualYRegister);
            virtualProgramCounter += 2;
            break;
        case (uint8_t)0xb4:
            actingAddressShort = addressingModeZeroPageIndexedWithX();
            if (printInstructions) printf("LDY with data at 0x%02x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            virtualYRegister = virtualMemoryMap[actingAddressShort];
            setNegativeFlagFromValue(virtualYRegister);
            setZeroFlagFromValue(virtualYRegister);
            virtualProgramCounter += 2;
            break;
        //STORE
            //STORE ACC
        case (uint8_t)0x8d:
            actingAddressShort = addressingModeAbsolute();
            if (printInstructions) printf("STA data at 0x%04x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            virtualMemoryMap[actingAddressShort] = virtualAccumulator;
            virtualProgramCounter += 3;
            break;
        case (uint8_t)0x9d:
            actingAddressShort = addressingModeAbsoluteIndexedWithX();
            if (printInstructions) printf("STA data at 0x%04x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            virtualMemoryMap[actingAddressShort] = virtualAccumulator;
            virtualProgramCounter += 3;
            break;
        case (uint8_t)0x99:
            actingAddressShort = addressingModeAbsoluteIndexedWithY();
            if (printInstructions) printf("STA data at 0x%04x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            virtualMemoryMap[actingAddressShort] = virtualAccumulator;
            virtualProgramCounter += 3;
            break;
        case (uint8_t)0x85:
            actingAddressShort = addressingModeZeroPage();
            if (printInstructions) printf("STA data at 0x%02x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            virtualMemoryMap[actingAddressShort] = virtualAccumulator;
            virtualProgramCounter += 2;
            break;
        case (uint8_t)0x81:
            actingAddressShort = addressingModeZeroPageIndexedIndirect();
            if (printInstructions) printf("STA data at 0x%02x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            virtualMemoryMap[actingAddressShort] = virtualAccumulator;
            virtualProgramCounter += 2;
            break;
        case (uint8_t)0x95:
            actingAddressShort = addressingModeZeroPageIndexedWithX();
            if (printInstructions) printf("STA data at 0x%02x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            virtualMemoryMap[actingAddressShort] = virtualAccumulator;
            virtualProgramCounter += 2;
            break;
        case (uint8_t)0x91:
            actingAddressShort = addressingModeZeroPageIndirectIndexedWithY();
            if (printInstructions) printf("STA data at 0x%02x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            virtualMemoryMap[actingAddressShort] = virtualAccumulator;
            virtualProgramCounter += 2;
            break;
            //STORE X
        case (uint8_t)0x8e:
            actingAddressShort = addressingModeAbsolute();
            if (printInstructions) printf("STX data at 0x%04x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            virtualMemoryMap[actingAddressShort] = virtualXRegister;
            virtualProgramCounter += 3;
            break;
        case (uint8_t)0x86:
            actingAddressShort = addressingModeZeroPage();
            if (printInstructions) printf("STX data at 0x%02x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            virtualMemoryMap[actingAddressShort] = virtualXRegister;
            virtualProgramCounter += 2;
            break;
        case (uint8_t)0x96:
            actingAddressShort = addressingModeZeroPageIndexedWithY();
            if (printInstructions) printf("STX data at 0x%02x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            virtualMemoryMap[actingAddressShort] = virtualXRegister;
            virtualProgramCounter += 2;
            break;
            //STORE Y
        case (uint8_t)0x8c:
            actingAddressShort = addressingModeAbsolute();
            if (printInstructions) printf("STY data at 0x%04x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            virtualMemoryMap[actingAddressShort] = virtualYRegister;
            virtualProgramCounter += 3;
            break;
        case (uint8_t)0x84:
            actingAddressShort = addressingModeZeroPage();
            if (printInstructions) printf("STY data at 0x%02x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            virtualMemoryMap[actingAddressShort] = virtualYRegister;
            virtualProgramCounter += 2;
            break;
        case (uint8_t)0x94:
            actingAddressShort = addressingModeZeroPageIndexedWithX();
            if (printInstructions) printf("STY data at 0x%02x (value 0x%02x)",actingAddressShort,virtualMemoryMap[actingAddressShort]);
            virtualMemoryMap[actingAddressShort] = virtualYRegister;
            virtualProgramCounter += 2;
            break;
        //SHIFT AND ROTATE
            //SHIFT LEFT
        case (uint8_t)0x0e:
            if (printInstructions) printf("ASL Arithmetic Shift Left");
            actingAddressShort = addressingModeAbsolute();
            if (virtualMemoryMap[actingAddressShort] & bitSevenTest) {
                setCarryFlag();
            } else {
                clearCarryFlag();
            }
            virtualMemoryMap[actingAddressShort] = (virtualMemoryMap[actingAddressShort] << 1);
            setNegativeFlagFromValue(virtualMemoryMap[actingAddressShort]);
            setZeroFlagFromValue(virtualMemoryMap[actingAddressShort]);
            virtualProgramCounter += 3;
            break;
        case (uint8_t)0x1e:
            if (printInstructions) printf("ASL Arithmetic Shift Left");
            actingAddressShort = addressingModeAbsoluteIndexedWithX();
            if (virtualMemoryMap[actingAddressShort] & bitSevenTest) {
                setCarryFlag();
            } else {
                clearCarryFlag();
            }
            virtualMemoryMap[actingAddressShort] = (virtualMemoryMap[actingAddressShort] << 1);
            setNegativeFlagFromValue(virtualMemoryMap[actingAddressShort]);
            setZeroFlagFromValue(virtualMemoryMap[actingAddressShort]);
            virtualProgramCounter += 3;
            break;
        case (uint8_t)0x0a:
            if (printInstructions) printf("ASL Arithmetic Shift Left on Accumulator");
            if (virtualAccumulator & bitSevenTest) {
                setCarryFlag();
            } else {
                clearCarryFlag();
            }
            virtualAccumulator = (virtualAccumulator << 1);
            setNegativeFlagFromValue(virtualAccumulator);
            setZeroFlagFromValue(virtualAccumulator);
            virtualProgramCounter++;
            break;
        case (uint8_t)0x06:
            if (printInstructions) printf("ASL Arithmetic Shift Left");
            actingAddressShort = addressingModeZeroPage();
            if (virtualMemoryMap[actingAddressShort] & bitSevenTest) {
                setCarryFlag();
            } else {
                clearCarryFlag();
            }
            virtualMemoryMap[actingAddressShort] = (virtualMemoryMap[actingAddressShort] << 1);
            setNegativeFlagFromValue(virtualMemoryMap[actingAddressShort]);
            setZeroFlagFromValue(virtualMemoryMap[actingAddressShort]);
            virtualProgramCounter += 2;
            break;
        case (uint8_t)0x16:
            if (printInstructions) printf("ASL Arithmetic Shift Left");
            actingAddressShort = addressingModeZeroPageIndexedWithX();
            if (virtualMemoryMap[actingAddressShort] & bitSevenTest) {
                setCarryFlag();
            } else {
                clearCarryFlag();
            }
            virtualMemoryMap[actingAddressShort] = (virtualMemoryMap[actingAddressShort] << 1);
            setNegativeFlagFromValue(virtualMemoryMap[actingAddressShort]);
            setZeroFlagFromValue(virtualMemoryMap[actingAddressShort]);
            virtualProgramCounter += 2;
            break;
            //SHIFT RIGHT
        case (uint8_t)0x4e:
            if (printInstructions) printf("LSR Logical Shift Right");
            actingAddressShort = addressingModeAbsolute();
            if (virtualMemoryMap[actingAddressShort] & bitZeroTest) {
                setCarryFlag();
            } else {
                clearCarryFlag();
            }
            virtualMemoryMap[actingAddressShort] = (virtualMemoryMap[actingAddressShort] >> 1);
            setNegativeFlagFromValue(virtualMemoryMap[actingAddressShort]);
            setZeroFlagFromValue(virtualMemoryMap[actingAddressShort]);
            virtualProgramCounter += 3;
            break;
        case (uint8_t)0x5e:
            if (printInstructions) printf("LSR Logical Shift Right");
            actingAddressShort = addressingModeAbsoluteIndexedWithX();
            if (virtualMemoryMap[actingAddressShort] & bitZeroTest) {
                setCarryFlag();
            } else {
                clearCarryFlag();
            }
            virtualMemoryMap[actingAddressShort] = (virtualMemoryMap[actingAddressShort] >> 1);
            setNegativeFlagFromValue(virtualMemoryMap[actingAddressShort]);
            setZeroFlagFromValue(virtualMemoryMap[actingAddressShort]);
            virtualProgramCounter += 3;
            break;
        case (uint8_t)0x4a:
            if (printInstructions) printf("LSR Logical Shift Right on Accumulator");
            if (virtualAccumulator & bitZeroTest) {
                setCarryFlag();
            } else {
                clearCarryFlag();
            }
            virtualAccumulator = (virtualAccumulator >> 1);
            setNegativeFlagFromValue(virtualAccumulator);
            setZeroFlagFromValue(virtualAccumulator);
            virtualProgramCounter++;
            break;
        case (uint8_t)0x46:
            if (printInstructions) printf("LSR Logical Shift Right");
            actingAddressShort = addressingModeZeroPage();
            if (virtualMemoryMap[actingAddressShort] & bitZeroTest) {
                setCarryFlag();
            } else {
                clearCarryFlag();
            }
            virtualMemoryMap[actingAddressShort] = (virtualMemoryMap[actingAddressShort] >> 1);
            setNegativeFlagFromValue(virtualMemoryMap[actingAddressShort]);
            setZeroFlagFromValue(virtualMemoryMap[actingAddressShort]);
            virtualProgramCounter += 2;
            break;
        case (uint8_t)0x56:
            if (printInstructions) printf("LSR Logical Shift Right");
            actingAddressShort = addressingModeZeroPageIndexedWithX();
            if (virtualMemoryMap[actingAddressShort] & bitZeroTest) {
                setCarryFlag();
            } else {
                clearCarryFlag();
            }
            virtualMemoryMap[actingAddressShort] = (virtualMemoryMap[actingAddressShort] >> 1);
            setNegativeFlagFromValue(virtualMemoryMap[actingAddressShort]);
            setZeroFlagFromValue(virtualMemoryMap[actingAddressShort]);
            virtualProgramCounter += 2;
            break;
            //ROTATE LEFT
        case (uint8_t)0x2e:
            if (printInstructions) printf("ROL Rotate Left One Bit");
            actingAddressShort = addressingModeAbsolute();
            if (virtualMemoryMap[actingAddressShort] & bitSevenTest) {
                virtualMemoryMap[actingAddressShort] = (virtualMemoryMap[actingAddressShort] << 1);
                virtualMemoryMap[actingAddressShort] |= 1UL << 0; // Set bit to 1
                setCarryFlag();
            } else {
                virtualMemoryMap[actingAddressShort] = (virtualMemoryMap[actingAddressShort] << 1);
                virtualMemoryMap[actingAddressShort] &= ~(1UL << 0); // Set bit to 0
                clearCarryFlag();
            }
            setNegativeFlagFromValue(virtualMemoryMap[actingAddressShort]);
            setZeroFlagFromValue(virtualMemoryMap[actingAddressShort]);
            virtualProgramCounter += 3;
            break;
        case (uint8_t)0x3e:
            if (printInstructions) printf("ROL Rotate Left One Bit");
            actingAddressShort = addressingModeAbsoluteIndexedWithX();
            if (virtualMemoryMap[actingAddressShort] & bitSevenTest) {
                virtualMemoryMap[actingAddressShort] = (virtualMemoryMap[actingAddressShort] << 1);
                virtualMemoryMap[actingAddressShort] |= 1UL << 0; // Set bit to 1
                setCarryFlag();
            } else {
                virtualMemoryMap[actingAddressShort] = (virtualMemoryMap[actingAddressShort] << 1);
                virtualMemoryMap[actingAddressShort] &= ~(1UL << 0); // Set bit to 0
                clearCarryFlag();
            }
            setNegativeFlagFromValue(virtualMemoryMap[actingAddressShort]);
            setZeroFlagFromValue(virtualMemoryMap[actingAddressShort]);
            virtualProgramCounter += 3;
            break;
        case (uint8_t)0x2a:
            if (printInstructions) printf("ROL Rotate Left One Bit on Accumulator");
            if (virtualAccumulator & bitSevenTest) {
                virtualAccumulator = (virtualAccumulator << 1);
                virtualAccumulator |= 1UL << 0; // Set bit to 1
                setCarryFlag();
            } else {
                virtualAccumulator = (virtualAccumulator << 1);
                virtualAccumulator &= ~(1UL << 0); // Set bit to 0
                clearCarryFlag();
            }
            setNegativeFlagFromValue(virtualAccumulator);
            setZeroFlagFromValue(virtualAccumulator);
            virtualProgramCounter++;
            break;
        case (uint8_t)0x26:
            if (printInstructions) printf("ROL Rotate Left One Bit");
            actingAddressShort = addressingModeZeroPage();
            if (virtualMemoryMap[actingAddressShort] & bitSevenTest) {
                virtualMemoryMap[actingAddressShort] = (virtualMemoryMap[actingAddressShort] << 1);
                virtualMemoryMap[actingAddressShort] |= 1UL << 0; // Set bit to 1
                setCarryFlag();
            } else {
                virtualMemoryMap[actingAddressShort] = (virtualMemoryMap[actingAddressShort] << 1);
                virtualMemoryMap[actingAddressShort] &= ~(1UL << 0); // Set bit to 0
                clearCarryFlag();
            }
            setNegativeFlagFromValue(virtualMemoryMap[actingAddressShort]);
            setZeroFlagFromValue(virtualMemoryMap[actingAddressShort]);
            virtualProgramCounter += 2;
            break;
        case (uint8_t)0x36:
            if (printInstructions) printf("ROL Rotate Left One Bit");
            actingAddressShort = addressingModeZeroPageIndexedWithX();
            if (virtualMemoryMap[actingAddressShort] & bitSevenTest) {
                virtualMemoryMap[actingAddressShort] = (virtualMemoryMap[actingAddressShort] << 1);
                virtualMemoryMap[actingAddressShort] |= 1UL << 0; // Set bit to 1
                setCarryFlag();
            } else {
                virtualMemoryMap[actingAddressShort] = (virtualMemoryMap[actingAddressShort] << 1);
                virtualMemoryMap[actingAddressShort] &= ~(1UL << 0); // Set bit to 0
                clearCarryFlag();
            }
            setNegativeFlagFromValue(virtualMemoryMap[actingAddressShort]);
            setZeroFlagFromValue(virtualMemoryMap[actingAddressShort]);
            virtualProgramCounter += 2;
            break;
            //ROTATE RIGHT
        case (uint8_t)0x6e:
            if (printInstructions) printf("ROR Rotate Right One Bit");
            actingAddressShort = addressingModeAbsolute();
            if (virtualMemoryMap[actingAddressShort] & bitZeroTest) {
                virtualMemoryMap[actingAddressShort] = (virtualMemoryMap[actingAddressShort] >> 1);
                virtualMemoryMap[actingAddressShort] |= 1UL << 7; // Set bit to 1
                setCarryFlag();
            } else {
                virtualMemoryMap[actingAddressShort] = (virtualMemoryMap[actingAddressShort] >> 1);
                virtualMemoryMap[actingAddressShort] &= ~(1UL << 7); // Set bit to 0
                clearCarryFlag();
            }
            setNegativeFlagFromValue(virtualMemoryMap[actingAddressShort]);
            setZeroFlagFromValue(virtualMemoryMap[actingAddressShort]);
            virtualProgramCounter += 3;
            break;
        case (uint8_t)0x7e:
            if (printInstructions) printf("ROR Rotate Right One Bit");
            actingAddressShort = addressingModeAbsoluteIndexedWithX();
            if (virtualMemoryMap[actingAddressShort] & bitZeroTest) {
                virtualMemoryMap[actingAddressShort] = (virtualMemoryMap[actingAddressShort] >> 1);
                virtualMemoryMap[actingAddressShort] |= 1UL << 7; // Set bit to 1
                setCarryFlag();
            } else {
                virtualMemoryMap[actingAddressShort] = (virtualMemoryMap[actingAddressShort] >> 1);
                virtualMemoryMap[actingAddressShort] &= ~(1UL << 7); // Set bit to 0
                clearCarryFlag();
            }
            setNegativeFlagFromValue(virtualMemoryMap[actingAddressShort]);
            setZeroFlagFromValue(virtualMemoryMap[actingAddressShort]);
            virtualProgramCounter += 3;
            break;
        case (uint8_t)0x6a:
            if (printInstructions) printf("ROR Rotate Right One Bit on Accumulator");
            if (virtualAccumulator & bitZeroTest) {
                virtualAccumulator = (virtualAccumulator >> 1);
                virtualAccumulator |= 1UL << 7; // Set bit to 1
                setCarryFlag();
            } else {
                virtualAccumulator = (virtualAccumulator >> 1);
                virtualAccumulator &= ~(1UL << 7); // Set bit to 0
                clearCarryFlag();
            }
            setNegativeFlagFromValue(virtualAccumulator);
            setZeroFlagFromValue(virtualAccumulator);
            virtualProgramCounter++;
            break;
        case (uint8_t)0x66:
            if (printInstructions) printf("ROR Rotate Right One Bit");
            actingAddressShort = addressingModeZeroPage();
            if (virtualMemoryMap[actingAddressShort] & bitZeroTest) {
                virtualMemoryMap[actingAddressShort] = (virtualMemoryMap[actingAddressShort] >> 1);
                virtualMemoryMap[actingAddressShort] |= 1UL << 7; // Set bit to 1
                setCarryFlag();
            } else {
                virtualMemoryMap[actingAddressShort] = (virtualMemoryMap[actingAddressShort] >> 1);
                virtualMemoryMap[actingAddressShort] &= ~(1UL << 7); // Set bit to 0
                clearCarryFlag();
            }
            setNegativeFlagFromValue(virtualMemoryMap[actingAddressShort]);
            setZeroFlagFromValue(virtualMemoryMap[actingAddressShort]);
            virtualProgramCounter += 2;
            break;
        case (uint8_t)0x76:
            if (printInstructions) printf("ROR Rotate Right One Bit");
            actingAddressShort = addressingModeZeroPageIndexedWithX();
            if (virtualMemoryMap[actingAddressShort] & bitZeroTest) {
                virtualMemoryMap[actingAddressShort] = (virtualMemoryMap[actingAddressShort] >> 1);
                virtualMemoryMap[actingAddressShort] |= 1UL << 7; // Set bit to 1
                setCarryFlag();
            } else {
                virtualMemoryMap[actingAddressShort] = (virtualMemoryMap[actingAddressShort] >> 1);
                virtualMemoryMap[actingAddressShort] &= ~(1UL << 7); // Set bit to 0
                clearCarryFlag();
            }
            setNegativeFlagFromValue(virtualMemoryMap[actingAddressShort]);
            setZeroFlagFromValue(virtualMemoryMap[actingAddressShort]);
            virtualProgramCounter += 2;
            break;
        //CONTROL
        case (uint8_t)0xea:
            if (printInstructions) printf("NOP No operation");
            virtualProgramCounter++;
            break;
        case (uint8_t)0x00:
            if (printInstructions) printf("BRK Program End");
            //BRK causes a non-maskable interrupt and increments the program counter by one. Therefore an RTI will go to the address of the BRK +2 so that BRK may be used to replace a two-byte instruction for debugging and the subsequent RTI will be correct. 
            virtualStatus |= 1UL << 4; // Set bit to 1
            virtualStatus |= 1UL << 2; // Set bit to 1
            halted = true;
            virtualProgramCounter++;
            break;
        default:
            printf("Unknown Instruction !! 0x%02x \n", virtualMemoryMap[virtualProgramCounter]);
            instructionFailed = true;
            virtualProgramCounter++;
    }
    if (printInstructions) printf("\n");
    return instructionFailed;
}

int main(int argc, char** argv) {
    std::cout << "BBC Micro Emulator v1.0.0\nCopyright (c) Alex Baldwin 2020.\n\n";
    
    bool printHelp = false;

	for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-p") {
            i++;
            std::string arg2 = argv[i];
            primaryRom = arg2;
            continue;
        }
        if (arg == "-s") {
            i++;
            std::string arg2 = argv[i];
            secondaryRom = arg2;
            continue;
        }
        if (arg == "-P") {
            i++;
            std::string arg2 = argv[i];
            std::stringstream ss;
            ss << std::hex << arg2.c_str();
            ss >> stepThroughAt;
            continue;
        }
        if (arg == "-f") {
            i++;
            std::string arg2 = argv[i];
            fullRom = arg2;
            continue;
        }
        if (arg == "-h" || arg == "--help") {
            printHelp = true;
            break;
        }
        //cartridgeRom = arg;
    }
    
    if (printHelp) {
        std::cout << "Select a Primary (Machine Operating System) ROM\n";
        std::cout << "  -p <romFile>\n";
        std::cout << "Select a Secondary (BBC BASIC) ROM\n";
        std::cout << "  -s <romFile>\n";
    } else {
        if (fullRom.length() == 0) {
            std::cout << "Primary ROM       : " << primaryRom << "\n";
            std::cout << "Secondary ROM     : " << secondaryRom << "\n";
            
            FILE *fileptr;
            uint8_t *buffer;
            int filelen;

            fileptr = fopen(primaryRom.c_str(), "rb");  // Open the file in binary mode
            fseek(fileptr, 0, SEEK_END);        // Jump to the end of the file
            filelen = ftell(fileptr);           // Get the current byte offset in the file
            rewind(fileptr);                    // Jump back to the beginning of the file

            buffer = (uint8_t *)malloc(filelen * sizeof(char)); // Enough memory for the file
            fread(buffer, filelen, 1, fileptr); // Read in the entire file
            fclose(fileptr); // Close the file
            
            for (int i = 0; i < (1024 * 16); i++) {
                virtualMemoryMap[i + 0xC000] = buffer[i]; // CONFIRMED CORRECT
            }

            fileptr = fopen(secondaryRom.c_str(), "rb");  // Open the file in binary mode
            fseek(fileptr, 0, SEEK_END);        // Jump to the end of the file
            filelen = ftell(fileptr);           // Get the current byte offset in the file
            rewind(fileptr);                    // Jump back to the beginning of the file

            buffer = (uint8_t *)malloc(filelen * sizeof(char)); // Enough memory for the file
            fread(buffer, filelen, 1, fileptr); // Read in the entire file
            fclose(fileptr); // Close the file
            
            for (int i = 0; i < (1024 * 16); i++) {
                virtualMemoryMap[i + 0x8000] = buffer[i];
            }
        } else {
            std::cout << "Full ROM          : " << fullRom << "\n";
            
            FILE *fileptr;
            uint8_t *buffer;
            int filelen;

            fileptr = fopen(fullRom.c_str(), "rb");  // Open the file in binary mode
            fseek(fileptr, 0, SEEK_END);        // Jump to the end of the file
            filelen = ftell(fileptr);           // Get the current byte offset in the file
            rewind(fileptr);                    // Jump back to the beginning of the file

            buffer = (uint8_t *)malloc(filelen * sizeof(char)); // Enough memory for the file
            fread(buffer, filelen, 1, fileptr); // Read in the entire file
            fclose(fileptr); // Close the file
            
            for (int i = 0; i < 0x8000; i++) {
                virtualMemoryMap[i + 0x8000] = buffer[i];
            }
        }
        std::cout << "Primary ROM       : " << primaryRom << "\n";
        std::cout << "Step Through At   : ";
        printf("0x%08x \n", stepThroughAt);
        
        std::cout << "\n";
        
        resetProcessor();
        
        std::cout << "\n";
        //std::cout << "Press enter to step processor";
        //std::cout << "\n";
        
        bool errorFound = false;
        
        uint32_t cycleCount = 0;
        
        while (true) {
            if (halted) {
                break;
            } else {
                if (stepThroughAt != 0) {
                    if (stepThroughAt == cycleCount) {
                        stepThrough = true;
                        printInstructions = true;
                        showDetailedDebugInfo = true;
                    }
                }
                
                if (showDetailedDebugInfo || errorFound || (cycleCount == 0) || stepThrough) {
                    std::cout << "\nProgram Counter   : ";
                    printf("0x%04x", virtualProgramCounter);
                    std::cout << "\nAccumulator       : ";
                    printf("0x%02x", virtualAccumulator);
                    std::cout << "\nX Register        : ";
                    printf("0x%02x", virtualXRegister);
                    std::cout << "\nY Register        : ";
                    printf("0x%02x", virtualYRegister);
                    std::cout << "\nStack Pointer     : ";
                    printf("0x%02x", virtualStackPointer);
                    std::cout << "\nInstruction Cycle : ";
                    printf("0x%08x", cycleCount);
                    std::cout << "\nFlags             : N V B D I Z C";
                    std::cout << "\n                    ";
                    if (isNegativeFlagSet()) { printf("Y "); } else { printf("n "); }
                    if (isOverflowFlagSet()) { printf("Y "); } else { printf("n "); }
                    if (isBreakFlagSet()) { printf("Y "); } else { printf("n "); }
                    if (isDecimalModeSet()) { printf("Y "); } else { printf("n "); }
                    if (isInterruptDisableSet()) { printf("Y "); } else { printf("n "); }
                    if (isZeroFlagSet()) { printf("Y "); } else { printf("n "); }
                    if (isCarryFlagSet()) { printf("Y "); } else { printf("n "); }
                    std::cout << "\nNearby Memory Map : " << "\n";
                    printAddress(virtualProgramCounter/16,(virtualProgramCounter/16) + 2);
                    std::cout << "\n";
                }

                if ((pauseOnError && errorFound) || stepThrough) {
                    getchar();
                }

                errorFound = stepVirtualProcessor();
                cycleCount++;
            }
        }
        std::cout << "Halted at         : ";
        printf("0x%04x", virtualProgramCounter);
        std::cout << "\n";
        std::cout << "Memory Map        : " << "\n";
        printAddress(virtualProgramCounter/16,(virtualProgramCounter/16) + 2);
    }

    //makePath(qemuConfigFolder);
    
    //std::ifstream t(vmList);
    //std::string options;

    //t.seekg(0, std::ios::end);
    //options.reserve(t.tellg());
    //t.seekg(0, std::ios::beg);

    //options.assign((std::istreambuf_iterator<char>(t)),
    //                std::istreambuf_iterator<char>());
    //vmOptions = json::parse(options);
    
    //for (auto& element : vmOptions) {
        //std::cout << element << '\n';
    //    VMObj vmobj(element);
    //    vms.push_back(vmobj);
    //}
    
    

    return 0;
}
