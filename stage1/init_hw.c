////////////////////////////////////////////////////////////////////////////////
///
/// @file       init_hw.c
///
/// @project
///
/// @brief      Hardware initialization routines for stage1
///
////////////////////////////////////////////////////////////////////////////////
///
////////////////////////////////////////////////////////////////////////////////
///
/// @copyright Copyright (c) 2018, Evan Lojewski
/// @cond
///
/// All rights reserved.
///
/// Redistribution and use in source and binary forms, with or without
/// modification, are permitted provided that the following conditions are met:
/// 1. Redistributions of source code must retain the above copyright notice,
/// this list of conditions and the following disclaimer.
/// 2. Redistributions in binary form must reproduce the above copyright notice,
/// this list of conditions and the following disclaimer in the documentation
/// and/or other materials provided with the distribution.
/// 3. Neither the name of the copyright holder nor the
/// names of its contributors may be used to endorse or promote products
/// derived from this software without specific prior written permission.
///
////////////////////////////////////////////////////////////////////////////////
///
/// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
/// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
/// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
/// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
/// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
/// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
/// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
/// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
/// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
/// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
/// POSSIBILITY OF SUCH DAMAGE.
/// @endcond
////////////////////////////////////////////////////////////////////////////////

#include "stage1.h"

#include <MII.h>
#include <bcm5719_DEVICE.h>
#include <bcm5719_GEN.h>
#include <bcm5719_RXMBUF.h>
#include <bcm5719_TXMBUF.h>
#include <bcm5719_SDBCACHE.h>
#include <stdbool.h>
#include <stdint.h>

void *memset(void *s, int c, size_t n)
{
#if CXX_SIMULATOR
    // TODO: Use the memory window to zero everything out.
#else
    while(--n)
    {
        *(uint32_t*)s = c;
    }
#endif
    return s;
}

static inline bool is_nic(void)
{
    // If DEVICE.Status.bits.VMAINPowerStatus is set we are in NIC mode,
    // otherwise LoM mode.
    return (1 == DEVICE.Status.bits.VMAINPowerStatus);
}

void init_mii_function0(void)
{
    // MIIPORT 0 (0x8010):0x1A |= 0x4000
    MII_selectBlock(0, 0x8010);
    uint16_t r1Ah_value = MII_readRegister(0, (mii_reg_t)0x1A);
    r1Ah_value |= 0x4000;
    MII_writeRegister(0, (mii_reg_t)0x1A, r1Ah_value);

    // (Note: This is done in a retry loop which verifies the block select by
    // reading 0x1F and confirming it reads 0x8610
    do
    {
        MII_selectBlock(0, 0x8610);
    } while (0x8610 != MII_getBlock(0));

    // MIIPORT 0 (0x8610):0x15, set bits 0:1 to 2.
    uint16_t r15h_value = MII_readRegister(0, (mii_reg_t)0x15);
    r15h_value &= ~0x3;
    r15h_value |= 0x2;
    MII_writeRegister(0, (mii_reg_t)0x15, r15h_value);

    // and then verifies that bits 0:1 have been set to 2, and retries about a
    // dozen times until the block select and write are both correct. Probably
    // an attempt to work around some bug or weird asynchronous behaviour for
    // these unknown MII registers.)
    do
    {
        r15h_value = MII_readRegister(0, (mii_reg_t)0x15);
    } while (2 != (r15h_value & 0x3));

    // (0x8010):0x1A, mask 0x4000.
    MII_selectBlock(0, 0x8010);
    r1Ah_value &= ~0x4000;
    MII_writeRegister(0, (mii_reg_t)0x1A, r1Ah_value);

    MII_selectBlock(0, 0);
}

void init_mii(void)
{
    //     MII init for all functions (MIIPORT determined by function/PHY type):
    // Set MII_REG_CONTROL to AUTO_NEGOTIATION_ENABLE.
    uint8_t phy = MII_getPhy();
    RegMIIControl_t control;
    control.r16 = MII_readRegister(phy, REG_MII_CONTROL);
    control.bits.AutoNegotiationEnable = 1;
    MII_writeRegister(phy, REG_MII_CONTROL, control.r16);
}

void early_init_hw(void)
{
    // Enable memory arbitration
    DEVICE.MemoryArbiterMode.bits.Enable = 1;

    // Disable data cache.
    DEVICE.RxRiscMode.bits.EnableDataCache = 0;

    // Enable various ape bits.
    RegDEVICEPciState_t pcistate = DEVICE.PciState;
    pcistate.bits.APEControlRegisterWriteEnable = 1;
    pcistate.bits.APESharedMemoryWriteEnable = 1;
    pcistate.bits.APEProgramSpaceWriteEnable = 1;
    DEVICE.PciState = pcistate;

    // Configure GPHY
    RegDEVICEGphyControlStatus_t gphystate = DEVICE.GphyControlStatus;
    gphystate.bits.GPHYIDDQ = 0;               // Power on GPHY
    gphystate.bits.BIASIDDQ = 0;               // Power on BIAS
    gphystate.bits.SGMII_DIV_PCSPowerDown = 0; // Power on SGMII
    gphystate.bits.TLPClockSource = 0;         // TLP Clock from PCIE SERDES

    if (is_nic())
    {
        // VMAIN ON, NIC (not LoM)
        gphystate.bits.TLPClockSource = 1;
    }

    DEVICE.GphyControlStatus = gphystate;
}


void init_mac(NVRAMContents_t *nvram)
{
    uint64_t mac0 = nvram->info.macAddr0;
    DEVICE.EmacMacAddresses0High.r32 = mac0 >> 32;
    DEVICE.EmacMacAddresses0Low.r32  = mac0;

    uint64_t mac1 = nvram->info.macAddr1;
    DEVICE.EmacMacAddresses1High.r32 = mac1 >> 32;
    DEVICE.EmacMacAddresses1Low.r32  = mac1;

    uint64_t mac2 = nvram->info2.macAddr2;
    DEVICE.EmacMacAddresses2High.r32 = mac2 >> 32;
    DEVICE.EmacMacAddresses2Low.r32  = mac2;

    uint64_t mac3 = nvram->info2.macAddr3;
    DEVICE.EmacMacAddresses3High.r32 = mac3 >> 32;
    DEVICE.EmacMacAddresses3Low.r32  = mac3;
}

uint32_t translate_power_budget(uint16_t raw)
{
    RegDEVICEPciPowerBudget0_t translator;
    translator.r32 = 0;
    if(raw)
    {
        translator.bits.BasePower = (raw) & 0xFF;
        translator.bits.DataScale = DEVICE_PCI_POWER_BUDGET_0_DATA_SCALE_0_1X;
        translator.bits.PMState   = ((raw) & 0x0300) >> 8;
        translator.bits.Type      = ((raw) & 0x1C00) >> 10;
        translator.bits.PowerRail = ((raw) & 0xE000) >> 13;

    }

    return translator.r32;
}

void init_power(NVRAMContents_t *nvram)
{
    // PCI power dissipated / consumed
    DEVICE.PciPowerConsumptionInfo.r32 = nvram->info.powerConsumed;
    DEVICE.PciPowerDissipatedInfo.r32  = nvram->info.powerDissipated;

    // Power Budget
    uint32_t pb_raw0 = (nvram->info.powerBudget0) & 0xffff;
    uint32_t pb_raw1 = (nvram->info.powerBudget0) >> 16;
    uint32_t pb_raw2 = (nvram->info.powerBudget1) & 0xffff;
    uint32_t pb_raw3 = (nvram->info.powerBudget1) >> 16;
    uint32_t pb_raw4 = (nvram->info.powerBudget2) & 0xffff;
    uint32_t pb_raw5 = (nvram->info.powerBudget2) >> 16;
    uint32_t pb_raw6 = (nvram->info.powerBudget3) & 0xffff;
    uint32_t pb_raw7 = (nvram->info.powerBudget3) >> 16;

    DEVICE.PciPowerBudget0.r32 = translate_power_budget(pb_raw0);
    DEVICE.PciPowerBudget1.r32 = translate_power_budget(pb_raw1);
    DEVICE.PciPowerBudget2.r32 = translate_power_budget(pb_raw2);
    DEVICE.PciPowerBudget3.r32 = translate_power_budget(pb_raw3);
    DEVICE.PciPowerBudget4.r32 = translate_power_budget(pb_raw4);
    DEVICE.PciPowerBudget5.r32 = translate_power_budget(pb_raw5);
    DEVICE.PciPowerBudget6.r32 = translate_power_budget(pb_raw6);
    DEVICE.PciPowerBudget7.r32 = translate_power_budget(pb_raw7);
}

void load_nvm_config(NVRAMContents_t *nvram)
{
    // Load information from NVM, set various registers + mem


    // MAC Addr.
    init_mac(nvram);

    // firmware revision
    // mfrDate


    // Power
    init_power(nvram);

    // REG_PCI_SUBSYSTEM_ID, vendor, class, rev
    //     uint16_t pciDevice;           //1  [ A0] 0x1657 BCM5719
    // uint16_t pciVendor;           //1  [ A2] 0x14E4 Broadcom
    // uint16_t pciSubsystem;        //1  [ A4] 0x1657 BCM5719     // Unused...
    // uint16_t pciSubsystemVendor;  //1  [ A6] 0x14E4 Broadcom

    //     uint32_t func0CfgFeature;     //1  [ C4] C5 C0 00 80 - Function 0 GEN_CFG_FEATURE.  FEATURE CONFIG
    // uint32_t func0CfgHW;          //1  [ C8] 00 00 40 14 - Function 0 GEN_CFG_HW.       HW CONFIG
    // uint32_t func1CfgFeature;     //1  [ D4] C5 C0 00 00 - Function 1 GEN_CFG_FEATURE.  FEATURE CONFIG
    // uint32_t func1CfgHW;          //1  [ D8] 00 00 40 14 - Function 1 GEN_CFG_HW.       HW CONFIG
    // uint32_t cfgShared;           //1  [ DC] 00 C2 AA 38 - GEN_CFG_SHARED.              SHARED CONFIG
    // uint32_t cfg5;                //1  [21C] 0   - GEN_CFG_5. g_unknownInitWord3
    // uint16_t pciSubsystemF1GPHY;  //1  [22C] 19 81 ] PCI Subsystem.
    // uint16_t pciSubsystemF0GPHY;  //1  [22E] 19 81 ] These are selected based on the
    // uint16_t pciSubsystemF2GPHY;  //1  [230] 19 81 ] function number and whether the NIC is a
    // uint16_t pciSubsystemF3GPHY;  //1  [232] 19 81 ] GPHY (copper) or SERDES (SFP) NIC.
    // uint16_t pciSubsystemF1SERDES;//1  [234] 16 57 ] BCM5719(?). Probably not programmed correctly
    // uint16_t pciSubsystemF0SERDES;//1  [236] 16 57 ] since Talos II doesn't use SERDES.
    // uint16_t pciSubsystemF3SERDES;//1  [238] 16 57 ]
    // uint16_t pciSubsystemF2SERDES;//1  [23A] 16 57 ]
    // uint32_t func2CfgFeature;     //1  [250] C5 C0 00 00 - Function 2 GEN_CFG_1E4.
    // uint32_t func2CfgHW;          //1  [254] 00 00 40 14 - Function 2 GEN_CFG_2.
    // uint32_t func3CfgFeature;     //1  [260] C5 C0 00 00 - Function 3 GEN_CFG_1E4.
    // uint32_t func3CfgHW;          //1  [264] 00 00 40 14 - Function 3 GEN_CFG_2.
    // uint32_t func0CfgHW2;         //1  [278] 00 00 00 40 - Function 0 GEN_CFG_2A8.
    // uint32_t func1CfgHW2;         //1  [27C] 00 00 00 40 - Function 1 GEN_CFG_2A8.
    // uint32_t func2CfgHW2;         //1  [280] 00 00 00 40 - Function 2 GEN_CFG_2A8.
    // uint32_t func3CfgHW2;         //1  [284] 00 00 00 40 - Function 3 GEN_CFG_2A8.
}

void init_hw(NVRAMContents_t *nvram)
{
    // Zero out ram - gencom, db cache, tx/rx mbuf, others in mem map
    memset((void*)&GEN, 0, REG_GEN_SIZE);
    memset((void*)&RXMBUF, 0, REG_RXMBUF_SIZE);
    memset((void*)&TXMBUF, 0, REG_TXMBUF_SIZE);
    memset((void*)&SDBCACHE, 0, REG_SDBCACHE_SIZE);

    // Misc regs init

    // Mask REG 0x64C0 bits 0x7FF, or bits 0x0010. This register is unknown.
    DEVICE._64c0.r32 = (DEVICE._64c0.r32 & ~0x7FFu) | 0x10;

    // Set unknown REG 0x64C8 to 0x1004.
    DEVICE._64c8.r32 = 0x00001004;

    // Enable MAC clock speed override
    RegDEVICEClockSpeedOverridePolicy_t clockspeed;
    clockspeed.r32 = 0;
    clockspeed.bits.MACClockSpeedOverrideEnabled = 1;
    DEVICE.ClockSpeedOverridePolicy = clockspeed;

    // Mask REG 0x64DC bits 0x0F, or bits 0x01. Unknown.
    DEVICE._64dc.r32 = (DEVICE._64dc.r32 & ~0xFu) | 0x01;

    // Mask REG 0x64DC bits 0xC00, set ... TODO
    // value from talos: 0x00315E42
    DEVICE._64dc.r32 &= ~0xC00;

    // Unknown stuff involving REG 0x6530, REG 0x65F4, depends on config, TODO
    // Value from Talos:0x6530z 0x6530 -> 0x00000000, 0x65F4 -> 0x00000109.


    // REG_LSO_NONLSO_BD_READ_DMA_CORRUPTION_ENABLE_CONTROL: Set BD and NonLSO
    // fields to 4K.
    RegDEVICELsoNonlsoBdReadDmaCorruptionEnableControl_t reglso = DEVICE.LsoNonlsoBdReadDmaCorruptionEnableControl;
    reglso.bits.PCIRequestBurstLengthforBDRDMAEngine = DEVICE_LSO_NONLSO_BD_READ_DMA_CORRUPTION_ENABLE_CONTROL_PCI_REQUEST_BURST_LENGTH_FOR_BD_RDMA_ENGINE_4K;
    reglso.bits.PCIRequestBurstLengthforNonLSORDMAEngine = DEVICE_LSO_NONLSO_BD_READ_DMA_CORRUPTION_ENABLE_CONTROL_PCI_REQUEST_BURST_LENGTH_FOR_NONLSO_RDMA_ENGINE_4K;
    DEVICE.LsoNonlsoBdReadDmaCorruptionEnableControl = reglso;

    // Disable ECC.
    RegDEVICEGphyStrap_t gphyStrap = DEVICE.GphyStrap;
    gphyStrap.bits.TXMBUFECCEnable = 0;
    gphyStrap.bits.RXMBUFECCEnable = 0;
    gphyStrap.bits.RXCPUSPADECCEnable = 0;
    DEVICE.GphyStrap = gphyStrap;

    // LED Control
    // Value from Talos: 0x00000880: DEVICE_LED_CONTROL_LED_STATUS_1000_MASK | DEVICE_LED_CONTROL_LED_MODE_PHY_MODE_1
    RegDEVICELedControl_t ledControl = DEVICE.LedControl;
    ledControl.bits.LEDMode = DEVICE_LED_CONTROL_LED_MODE_PHY_MODE_1;
    DEVICE.LedControl = ledControl;

    // MISC Local Control
    // Value from Talos: 0x00020001, reserved bits

    // Set REG_EAV_REF_CLOCK_CONTROL as desired. This is initialized from
    // CFG_HW; the TIMESYNC_GPIO_MAPPING, APE_GPIO_{0,1,2,3} fields within it
    // are copied to the corresponding fields in REG_EAV_REF_CLOCK_CONTROL.

    // Optionally enable REG_GRC_MODE_CONTROL__TIME_SYNC_MODE_ENABLE.
    // Value from Talos: 0x00130034
    // Bit is not set on Talos w/ default firmware, disabled for now.

    // Enable const clock for MII
    DEVICE.MiiMode.bits.ConstantMDIO_DIV_MDCClockSpeed = 1;

    // Set or clear REG_GPHY_CONTROL_STATUS__SWITCHING_REGULATOR_POWER_DOWN as
    // desired.
    // Value from Talos:  0x02C01000
    // Bit is not set on Talos w/ default firmware, disabled for now.

    // Set or clear
    // REG_TOP_LEVEL_MISCELLANEOUS_CONTROL_1__NCSI_CLOCK_OUTPUT_DISABLE as
    // desired.
    // Value from Talos: 0x00000080
    // Bit is not set on Talos w/ default firmware, disabled for now.

    // Perform MII init.
    init_mii_function0();

    init_mii();
}
