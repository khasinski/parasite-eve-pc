#include "common.h"
#include <psyz/audio.h>
#include "pe1/akao.h"
#include "pe1/akao/commands.h"
#include "pe1/akao/globals.h"
#include "pe1/akao/spu_registers.h"
#include "pe1/akao/spu_voice.h"
#include "pe1/akao/tick.h"
#include "pe1/akao/track.h"
#include "pe1/akao/voice_masks.h"
#include "pe1/akao/voice_state.h"
#include "pe1/psyq_cd_audio.h"
#include "pe1/psyq_spu_internal.h"

int Akao_Cmd_10(void *bank) {
    g_AkaoCmd.opcode = 0x10;
    g_AkaoCmd.arg0.sample_header = bank;
    return Akao_EnqueueStagedCommand();
}

void Akao_Cmd_11(int arg0) {
    g_AkaoCmd.opcode = 0x11;
    g_AkaoCmd.arg0.value = arg0;
    Akao_EnqueueStagedCommand();
}

void Akao_Cmd_40(void) {
    g_AkaoCmd.opcode = 0x40;
    Akao_EnqueueStagedCommand();
}

int Akao_Cmd_19_Then_C0(void *arg0, int arg1) {
    int *opcode;
    int saved_arg;
    int next_opcode;
    int ret;

    opcode = &g_AkaoCmd.opcode;
    *opcode = 0x19;
    g_AkaoCmd.arg0.sample_header = arg0;
    saved_arg = arg1;
    ret = Akao_EnqueueStagedCommand();

    next_opcode = 0xC0;
    *opcode = next_opcode;
    saved_arg &= 0x7F;
    g_AkaoCmd.arg0.value = saved_arg;
    g_AkaoCmd.arg3 = 0;
    saved_arg = ret;
    Akao_EnqueueStagedCommand();

    return saved_arg;
}

void Akao_Cmd_12(int arg0, int arg1) {
    g_AkaoCmd.opcode = 0x12;
    g_AkaoCmd.arg0.value = arg0;
    g_AkaoCmd.arg1 = arg1;
    Akao_EnqueueStagedCommand();
}

void Akao_Cmd_20(int arg0, int arg1, int arg2, int arg3) {
    g_AkaoCmd.opcode = 0x20;
    g_AkaoCmd.arg0.value = arg0 & 0x3FF;
    g_AkaoCmd.arg1 = arg1 & 0xFFFFFF;
    g_AkaoCmd.arg2 = arg2 & 0xFF;
    g_AkaoCmd.arg3 = arg3 & 0x7F;
    Akao_EnqueueStagedCommand();
}

int Akao_Cmd_24(void *header, int arg1, int arg2, int arg3) {
    int status = Spu_ValidateSampleHeader(header);
    if (status != 0) {
        return status;
    }

    g_AkaoCmd.opcode = 0x24;
    g_AkaoCmd.arg0.command_data = ((AkaoFilePrefix *)header)->contents;
    g_AkaoCmd.arg1 = arg1 & 0xFFFFFF;
    g_AkaoCmd.arg2 = arg2 & 0xFF;
    g_AkaoCmd.arg3 = arg3 & 0x7F;
    return Akao_EnqueueStagedCommand();
}

void Akao_Cmd_21(int arg0, int arg1) {
    g_AkaoCmd.opcode = 0x21;
    g_AkaoCmd.arg0.value = arg0 & 0xFFFF;
    g_AkaoCmd.arg1 = arg1 & 0xFFFFFF;
    Akao_EnqueueStagedCommand();
}

void Akao_Cmd_30(int arg0) {
    g_AkaoCmd.opcode = 0x30;
    g_AkaoCmd.arg0.value = arg0 & 0x3FF;
    Akao_EnqueueStagedCommand();
}

void Akao_Cmd_80_81_82(int arg0) {
    int opcode;

    switch (arg0) {
    case 1:
        opcode = 0x81;
        break;
    case 2:
        opcode = 0x82;
        break;
    default:
        opcode = 0x80;
        break;
    }

    g_AkaoCmd.opcode = opcode;
    Akao_EnqueueStagedCommand();
}

void Akao_Cmd_90(int arg0) {
    g_AkaoCmd.opcode = 0x90;
    g_AkaoCmd.arg0.value = arg0 & 0xFFFFFF;
    Akao_EnqueueStagedCommand();
}

void Akao_Cmd_92(int arg0) {
    g_AkaoCmd.opcode = 0x92;
    g_AkaoCmd.arg0.value = arg0;
    Akao_EnqueueStagedCommand();
}

void Akao_Cmd_99_9B_9D(int arg0) {
    int opcode;

    switch (arg0) {
    case 1:
        opcode = 0x9B;
        break;
    case 2:
        opcode = 0x9D;
        break;
    default:
        opcode = 0x99;
        break;
    }

    g_AkaoCmd.opcode = opcode;
    Akao_EnqueueStagedCommand();
}

void Akao_Cmd_98_9A_9C(int arg0) {
    int opcode;

    switch (arg0) {
    case 1:
        opcode = 0x9A;
        break;
    case 2:
        opcode = 0x9C;
        break;
    default:
        opcode = 0x98;
        break;
    }

    g_AkaoCmd.opcode = opcode;
    Akao_EnqueueStagedCommand();
}

void Akao_Cmd_A8(int arg0) {
    g_AkaoCmd.opcode = 0xA8;
    g_AkaoCmd.arg0.value = arg0 & 0x7F;
    Akao_EnqueueStagedCommand();
}

void Akao_Cmd_A9(int arg0, int arg1) {
    g_AkaoCmd.opcode = 0xA9;
    g_AkaoCmd.arg0.value = arg0 & 0xFF;
    g_AkaoCmd.arg1 = arg1 & 0x7F;
    Akao_EnqueueStagedCommand();
}

void Akao_Cmd_A0(int arg0, int arg1, int arg2) {
    g_AkaoCmd.opcode = 0xA0;
    g_AkaoCmd.arg0.value = arg0 & 0xFFFF;
    g_AkaoCmd.arg1 = arg1 & 0xFFFFFF;
    g_AkaoCmd.arg2 = arg2 & 0x7F;
    Akao_EnqueueStagedCommand();
}

void Akao_Cmd_A1(int arg0, int arg1, int arg2, int arg3) {
    g_AkaoCmd.opcode = 0xA1;
    g_AkaoCmd.arg0.value = arg0 & 0xFFFF;
    g_AkaoCmd.arg1 = arg1 & 0xFFFFFF;
    g_AkaoCmd.arg2 = arg2 & 0xFF;
    g_AkaoCmd.arg3 = arg3 & 0x7F;
    Akao_EnqueueStagedCommand();
}

void Akao_Cmd_AA(int arg0) {
    g_AkaoCmd.opcode = 0xAA;
    g_AkaoCmd.arg0.value = arg0 & 0xFF;
    Akao_EnqueueStagedCommand();
}

void Akao_Cmd_AB(int arg0, int arg1) {
    g_AkaoCmd.opcode = 0xAB;
    g_AkaoCmd.arg0.value = arg0 & 0xFF;
    g_AkaoCmd.arg1 = arg1 & 0xFF;
    Akao_EnqueueStagedCommand();
}

void Akao_Cmd_A2(int arg0, int arg1, int arg2) {
    g_AkaoCmd.opcode = 0xA2;
    g_AkaoCmd.arg0.value = arg0 & 0x3FF;
    g_AkaoCmd.arg1 = arg1 & 0xFFFFFF;
    g_AkaoCmd.arg2 = arg2 & 0xFF;
    Akao_EnqueueStagedCommand();
}

void Akao_Cmd_A3(int arg0, int arg1, int arg2, int arg3) {
    g_AkaoCmd.opcode = 0xA3;
    g_AkaoCmd.arg0.value = arg0 & 0x3FF;
    g_AkaoCmd.arg1 = arg1 & 0xFFFFFF;
    g_AkaoCmd.arg2 = arg2 & 0xFF;
    g_AkaoCmd.arg3 = arg3 & 0xFF;
    Akao_EnqueueStagedCommand();
}

void Akao_Cmd_AC(int arg0) {
    g_AkaoCmd.opcode = 0xAC;
    g_AkaoCmd.arg0.value = arg0 & 0xFF;
    Akao_EnqueueStagedCommand();
}

void Akao_Cmd_AD(int arg0, int arg1) {
    g_AkaoCmd.opcode = 0xAD;
    g_AkaoCmd.arg0.value = arg0 & 0xFF;
    g_AkaoCmd.arg1 = arg1 & 0xFF;
    Akao_EnqueueStagedCommand();
}

void Akao_Cmd_A4(int arg0, int arg1, int arg2) {
    g_AkaoCmd.opcode = 0xA4;
    g_AkaoCmd.arg0.value = arg0 & 0xFFFF;
    g_AkaoCmd.arg1 = arg1 & 0xFFFFFF;
    g_AkaoCmd.arg2 = arg2 & 0xFF;
    Akao_EnqueueStagedCommand();
}

void Akao_Cmd_A5(int arg0, int arg1, int arg2, int arg3) {
    g_AkaoCmd.opcode = 0xA5;
    g_AkaoCmd.arg0.value = arg0 & 0xFFFF;
    g_AkaoCmd.arg1 = arg1 & 0xFFFFFF;
    g_AkaoCmd.arg2 = arg2 & 0xFF;
    g_AkaoCmd.arg3 = arg3 & 0xFF;
    Akao_EnqueueStagedCommand();
}

void Akao_Cmd_C0_WithSlot(int arg0, int arg1) {
    g_AkaoCmd.opcode = 0xC0;
    g_AkaoCmd.arg0.value = arg1 & 0x7F;
    g_AkaoCmd.arg3 = arg0;
    Akao_EnqueueStagedCommand();
}

void Akao_Cmd_C1_WithSlot(int arg0, int arg1, int arg2) {
    g_AkaoCmd.opcode = 0xC1;
    g_AkaoCmd.arg0.value = arg1;
    g_AkaoCmd.arg1 = arg2 & 0x7F;
    g_AkaoCmd.arg3 = arg0;
    Akao_EnqueueStagedCommand();
}

void Akao_Cmd_C2_WithSlot(int arg0, int arg1, int arg2, int arg3) {
    g_AkaoCmd.opcode = 0xC2;
    g_AkaoCmd.arg0.value = arg1;
    g_AkaoCmd.arg1 = arg2 & 0x7F;
    g_AkaoCmd.arg2 = arg3 & 0x7F;
    g_AkaoCmd.arg3 = arg0;
    Akao_EnqueueStagedCommand();
}

void Akao_Cmd_C8(int arg0) {
    g_AkaoCmd.opcode = 0xC8;
    g_AkaoCmd.arg0.value = arg0;
    Akao_EnqueueStagedCommand();
}

void Akao_Cmd_C9(int arg0, int arg1) {
    g_AkaoCmd.opcode = 0xC9;
    g_AkaoCmd.arg0.value = arg0;
    g_AkaoCmd.arg1 = arg1;
    Akao_EnqueueStagedCommand();
}

void Akao_Cmd_CA(int arg0, int arg1, int arg2) {
    g_AkaoCmd.opcode = 0xCA;
    g_AkaoCmd.arg0.value = arg0;
    g_AkaoCmd.arg1 = arg1;
    g_AkaoCmd.arg2 = arg2;
    Akao_EnqueueStagedCommand();
}

void Akao_Cmd_D0(int arg0) {
    g_AkaoCmd.opcode = 0xD0;
    g_AkaoCmd.arg0.value = arg0 & 0xFF;
    Akao_EnqueueStagedCommand();
}

void Akao_Cmd_D1(int arg0, int arg1) {
    g_AkaoCmd.opcode = 0xD1;
    g_AkaoCmd.arg0.value = arg0;
    g_AkaoCmd.arg1 = arg1 & 0xFF;
    Akao_EnqueueStagedCommand();
}

void Akao_Cmd_D2(int arg0, int arg1, int arg2) {
    g_AkaoCmd.opcode = 0xD2;
    g_AkaoCmd.arg0.value = arg0;
    g_AkaoCmd.arg1 = arg1 & 0xFF;
    g_AkaoCmd.arg2 = arg2 & 0xFF;
    Akao_EnqueueStagedCommand();
}

void Akao_Cmd_D4(int arg0) {
    g_AkaoCmd.opcode = 0xD4;
    g_AkaoCmd.arg0.value = arg0 & 0xFF;
    Akao_EnqueueStagedCommand();
}

void Akao_Cmd_D5(int arg0, int arg1) {
    g_AkaoCmd.opcode = 0xD5;
    g_AkaoCmd.arg0.value = arg0;
    g_AkaoCmd.arg1 = arg1 & 0xFF;
    Akao_EnqueueStagedCommand();
}

void Akao_Cmd_D6(int arg0, int arg1, int arg2) {
    g_AkaoCmd.opcode = 0xD6;
    g_AkaoCmd.arg0.value = arg0;
    g_AkaoCmd.arg1 = arg1 & 0xFF;
    g_AkaoCmd.arg2 = arg2 & 0xFF;
    Akao_EnqueueStagedCommand();
}

void Akao_Cmd_D8(int arg0) {
    g_AkaoCmd.opcode = 0xD8;
    g_AkaoCmd.arg0.value = arg0 & 0xFF;
    Akao_EnqueueStagedCommand();
}

void Akao_Cmd_D9(int arg0, int arg1) {
    g_AkaoCmd.opcode = 0xD9;
    g_AkaoCmd.arg0.value = arg0;
    g_AkaoCmd.arg1 = arg1 & 0xFF;
    Akao_EnqueueStagedCommand();
}

void Akao_Cmd_DA(int arg0, int arg1, int arg2) {
    g_AkaoCmd.opcode = 0xDA;
    g_AkaoCmd.arg0.value = arg0;
    g_AkaoCmd.arg1 = arg1 & 0xFF;
    g_AkaoCmd.arg2 = arg2 & 0xFF;
    Akao_EnqueueStagedCommand();
}

void Akao_Cmd_F0(void) {
    g_AkaoCmd.opcode = 0xF0;
    Akao_EnqueueStagedCommand();
}

void Akao_Cmd_F1(void) {
    g_AkaoCmd.opcode = 0xF1;
    Akao_EnqueueStagedCommand();
}

int Spu_PollOrWaitSeqBusy(int mode) {
    if (mode == 0) {
        while (g_AkaoSeqBusyFlags & 1) {
        }
        return 0;
    }

    return g_AkaoSeqBusyFlags & 1;
}

int Spu_UploadSampleBlockBlocking(void *arg0, int arg1) {
    int result;

    while ((result = Spu_UploadSampleBlock(arg0, arg1)) == 1) {
    }
    return result;
}

int Spu_GetTransferStatus(void) {
    return g_SpuTransferStatus;
}

static inline unsigned char ScaleVolume(int volume) {
    unsigned int value = volume;

    value <<= 1;
    value += volume;
    value <<= 2;
    value -= volume;
    value += value << 5;
    value <<= 3;
    value -= volume;
    return value >> 13;
}

void Spu_SetVoiceVolume(int arg0) {
    unsigned char value;

    if (g_AkaoPlaybackMode & 2) {
        value = ScaleVolume(arg0);
        g_AkaoCdVolume.val3 = value;
        g_AkaoCdVolume.val1 = value;
        g_AkaoCdVolume.val2 = value;
        g_AkaoCdVolume.val0 = value;
    } else {
        value = arg0;
        g_AkaoCdVolume.val2 = value;
        g_AkaoCdVolume.val0 = value;
        g_AkaoCdVolume.val3 = 0;
        g_AkaoCdVolume.val1 = 0;
    }
    CdMix(&g_AkaoCdVolume);
}

int Spu_SetStreamModeA(void) {
    g_AkaoStreamUploadMode = 1;
    return 0;
}

int Akao_StepNoteSequencer(unsigned char *input, unsigned available)
{
    unsigned char *instruments;
    unsigned payload_size;
    unsigned count;
    unsigned address;
    unsigned address_offset;
    unsigned upload_size;
    unsigned after_header;
    unsigned char *rebase_instruments;
    unsigned rebase_address;
    unsigned next_address;
    unsigned remaining_payload;
    unsigned start;
    unsigned *dest;
    AkaoInstrumentAddress cache;
    unsigned result;

    if (g_AkaoStreamUploadMode & 1) {
        if (Spu_ValidateSampleHeader(input)) return -1;
        input += 0x14;
        payload_size = *(unsigned *)input;
        input += 4;
        start = *(unsigned *)input;
        input += 4;
        count = *(unsigned *)input;
        if (!count) count = 256;
        count -= start;
        input += 0x24;
        instruments = input;
        upload_size = count << 6;
        input += upload_size;
        after_header = available - 0x40;
        available = after_header - upload_size;
        upload_size = available;
        if (upload_size >= payload_size) upload_size = payload_size;

        if (count < 0x31) {
            if (g_AkaoCurTrack[1].active_voice_mask != 0) {
                if ((g_AkaoCurTrack[1].status_flags & 0x100) == 0) {
                    address = 0x38000;
                    g_AkaoVoiceKeyOnState |= 0x100;
                } else {
                    address = 0x8000;
                    g_AkaoVoiceKeyOnState &= ~0x100U;
                }
            } else {
                address = 0x8000;
                if (g_AkaoCurTrack[0].active_voice_mask != 0) {
                    if ((g_AkaoCurTrack[0].status_flags & 0x100) == 0) {
                        address = 0x38000;
                        g_AkaoVoiceKeyOnState |= 0x100;
                    } else {
                        g_AkaoVoiceKeyOnState &= ~0x100U;
                    }
                } else {
                    g_AkaoVoiceKeyOnState &= ~0x100U;
                }
            }
        } else {
            address = 0x8000;
            g_AkaoVoiceKeyOnState &= ~0x100U;
        }
        SpuSetTransferStartAddr(address);
        Spu_UploadWithPrepare(input, upload_size);
        rebase_instruments = instruments;
        rebase_address = address;
        next_address = address + upload_size;
        g_AkaoInstrumentUploadAddress = next_address;
        remaining_payload = payload_size - upload_size;
        g_AkaoInstrumentUploadRemaining = remaining_payload;
        Spu_RebaseStreamAddrs(rebase_instruments, rebase_address, count);
        address_offset = address == 0x8000 ? 0x20 : 0x50;
        address_offset <<= 6;
        cache.instrument = g_AkaoInstrumentTable;
        cache.word = address_offset + cache.word;
        dest = cache.words;
        upload_size = count << 4;
        do {
            --upload_size;
            *dest++ = *(unsigned *)instruments;
            instruments += 4;
        } while (upload_size);
        g_AkaoStreamUploadMode &= ~1U;
        return g_AkaoInstrumentUploadRemaining;
    }
    SpuSetTransferStartAddr(g_AkaoInstrumentUploadAddress);
    upload_size = available;
    if (upload_size >= g_AkaoInstrumentUploadRemaining) upload_size = g_AkaoInstrumentUploadRemaining;
    Spu_UploadWithPrepare(input, upload_size);
    g_AkaoInstrumentUploadAddress += upload_size;
    g_AkaoInstrumentUploadRemaining -= upload_size;
    return g_AkaoInstrumentUploadRemaining;
}

int Spu_SetStreamModeB(void) {
    g_AkaoStreamUploadMode = 2;
    return 0;
}

int Spu_UploadStreamBlockA(unsigned bank, unsigned char *input, unsigned available)
{
    unsigned char *instruments;
    unsigned payload_size;
    unsigned count;
    unsigned address;
    unsigned upload_size;
    unsigned start;
    unsigned after_header;
    unsigned *dest;
    AkaoInstrumentAddress cache;

    if (g_AkaoStreamUploadMode & 2) {
        if (Spu_ValidateSampleHeader(input)) return -1;
        input += 0x14;
        payload_size = *(unsigned *)input;
        input += 4;
        start = *(unsigned *)input;
        input += 4;
        count = *(unsigned *)input;
        if (!count) count = 256;
        count -= start;
        input += 0x24;
        instruments = input;
        upload_size = count << 6;
        input += upload_size;
        after_header = available - 0x40;
        available = after_header - upload_size;
        upload_size = available;
        if (upload_size >= payload_size) upload_size = payload_size;
        address = ((bank * 5) << 13) + 0x4f000;
        SpuSetTransferStartAddr(address);
        Spu_UploadWithPrepare(input, upload_size);
        g_AkaoStreamAAddress = address + upload_size;
        g_AkaoStreamARemaining = payload_size - upload_size;
        Spu_RebaseStreamAddrs(instruments, address, count);
        address = bank << 10;
        cache.instrument = &g_AkaoStreamInstrumentCacheB[16];
        cache.word = address + cache.word;
        dest = cache.words;
        upload_size = count << 4;
        do {
            --upload_size;
            *dest++ = *(unsigned *)instruments;
            instruments += 4;
        } while (upload_size);
        g_AkaoStreamUploadMode &= ~2U;
        return g_AkaoStreamARemaining;
    }
    SpuSetTransferStartAddr(g_AkaoStreamAAddress);
    upload_size = available;
    if (upload_size >= g_AkaoStreamARemaining) upload_size = g_AkaoStreamARemaining;
    Spu_UploadWithPrepare(input, upload_size);
    g_AkaoStreamAAddress += upload_size;
    g_AkaoStreamARemaining -= upload_size;
    return g_AkaoStreamARemaining;
}

int Spu_UploadStreamBlockB(unsigned bank, unsigned char *cursor)
{
    unsigned char *instruments;
    unsigned size, start, count, address;
    AkaoInstrumentAddress cache;
    unsigned *dest;
    if (bank & ~1U) return 1;
    if (Spu_ValidateSampleHeader(cursor)) return -1;
    /* Header words: payload size, first instrument, exclusive end.
     * An encoded end of zero denotes instrument 256. */
    cursor += 0x14;
    size = *(unsigned *)cursor;
    cursor += 4;
    start = *(unsigned *)cursor;
    cursor += 4;
    count = *(unsigned *)cursor;
    if (!count) count = 256;
    count -= start;
    /* The 0x40-byte header is followed by 0x40-byte instrument records. */
    instruments = cursor + 0x24;
    cursor = instruments + (count << 6);
    address = 0x68000 + (bank << 13);
    SpuSetTransferStartAddr(address);
    Spu_UploadWithPrepare(cursor, size);
    Spu_RebaseStreamAddrs(instruments,address,count);
    /* Cache the relocated records after the SPU payload upload. */
    address = bank << 10;
    cache.instrument = g_AkaoStreamInstrumentCacheB;
    cache.word = address + cache.word;
    dest = cache.words;
    size = count << 4;
    do {
        --size;
        *dest++ = *(unsigned *)instruments;
        instruments += 4;
    } while (size);
    return 0;
}

void Spu_WriteKeyOn(u32 value) {
    Psyz_SpuWrite(PE1_OFFSETOF(SpuRegs, key_on[0]), (u16)value);
    Psyz_SpuWrite(PE1_OFFSETOF(SpuRegs, key_on[1]), (u16)(value >> 16));
}

void Spu_WriteKeyOff(u32 value) {
    Psyz_SpuWrite(PE1_OFFSETOF(SpuRegs, key_off[0]), (u16)value);
    Psyz_SpuWrite(PE1_OFFSETOF(SpuRegs, key_off[1]), (u16)(value >> 16));
}

void Spu_WriteReverbEnable(u32 value) {
    Psyz_SpuWrite(PE1_OFFSETOF(SpuRegs, reverb_enable[0]), (u16)value);
    Psyz_SpuWrite(PE1_OFFSETOF(SpuRegs, reverb_enable[1]), (u16)(value >> 16));
}

void Spu_WriteNoiseEnable(u32 value) {
    Psyz_SpuWrite(PE1_OFFSETOF(SpuRegs, noise_enable[0]), (u16)value);
    Psyz_SpuWrite(PE1_OFFSETOF(SpuRegs, noise_enable[1]), (u16)(value >> 16));
}

void Spu_WriteFmEnable(u32 value) {
    Psyz_SpuWrite(PE1_OFFSETOF(SpuRegs, pitch_modulation[0]), (u16)value);
    Psyz_SpuWrite(PE1_OFFSETOF(SpuRegs, pitch_modulation[1]), (u16)(value >> 16));
}

void AkaoSpuVoice_SetVolume(u32 index, u32 left, u32 right) {
    Psyz_AudioLock();
    volatile u16 *ptr;
    u32 mask;

    ptr = &_spu_RXX->voice[index].volume_left;
    mask = 0x7FFF;
    ptr[0] = left & mask;
    ptr++;
    *ptr = right & 0x7FFF;
    Psyz_AudioUnlock();
}

void AkaoSpuVoice_SetPitch(u32 index, u32 value) {
    Psyz_AudioLock();
    volatile u16 *ptr;

    ptr = &_spu_RXX->voice[index].volume_left;
    ptr[2] = value;
    Psyz_AudioUnlock();
}

void AkaoSpuVoice_SetStartAddress(u32 index, u32 value) {
    Psyz_AudioLock();
    volatile u16 *ptr;

    ptr = &_spu_RXX->voice[index].volume_left;
    ptr[3] = value >> 3;
    Psyz_AudioUnlock();
}

void AkaoSpuVoice_SetRepeatAddress(u32 index, u32 value) {
    Psyz_AudioLock();
    volatile u16 *ptr;

    ptr = &_spu_RXX->voice[index].volume_left;
    ptr[7] = value >> 3;
    Psyz_AudioUnlock();
}

void AkaoSpuVoice_SetAdsrAttack(u32 index, u32 left, u32 right) {
    Psyz_AudioLock();
    volatile u16 *ptr;
    u32 current;
    u32 value;

    ptr = &_spu_RXX->voice[index].adsr_low;
    right = (right >> 2) << 15;
    left <<= 8;
    current = *ptr & 0xFF;
    value = right | left;
    *ptr = current | value;
    Psyz_AudioUnlock();
}

void AkaoSpuVoice_SetAdsrDecayRate(u32 index, u32 value) {
    Psyz_AudioLock();
    volatile u16 *ptr;
    u32 current;

    ptr = &_spu_RXX->voice[index].adsr_low;
    current = *ptr;
    value <<= 4;
    current &= 0xFF0F;
    *ptr = current | value;
    Psyz_AudioUnlock();
}

void AkaoSpuVoice_SetAdsrSustainLevel(u32 index, u32 value) {
    Psyz_AudioLock();
    volatile u16 *ptr;
    u32 current;

    ptr = &_spu_RXX->voice[index].adsr_low;
    current = *ptr;
    current &= 0xFFF0;
    *ptr = current | value;
    Psyz_AudioUnlock();
}

void AkaoSpuVoice_SetAdsrSustainRate(u32 index, u32 left, u32 right) {
    Psyz_AudioLock();
    volatile u16 *ptr;
    u32 current;
    u32 value;

    ptr = &_spu_RXX->voice[index].adsr_high;
    right = (right >> 1) << 14;
    left <<= 6;
    current = *ptr;
    value = right | left;
    current &= 0x3F;
    *ptr = current | value;
    Psyz_AudioUnlock();
}

void AkaoSpuVoice_SetAdsrReleaseRate(u32 index, u32 left, u32 right) {
    Psyz_AudioLock();
    volatile u16 *ptr;
    u32 current;
    u32 value;

    ptr = &_spu_RXX->voice[index].adsr_high;
    right = (right >> 2) << 5;
    current = *ptr;
    value = right | left;
    current &= 0xFFC0;
    *ptr = current | value;
    Psyz_AudioUnlock();
}

static int s_music_muted;

void Akao_SetMusicMuted(int muted)
{
    muted = muted != 0;
    if (muted == s_music_muted) return;
    s_music_muted = muted;
    for (unsigned i = 0; i < AKAO_VOICE_COUNT; i++) {
        g_AkaoVoiceStateTable[i].update_flags |= AKAO_VOICE_PARAM_VOLUME;
        g_AkaoVoiceStateTable2[i].update_flags |= AKAO_VOICE_PARAM_VOLUME;
    }
}

static void write_voice_param(int voice_index, AkaoVoiceParams *params, int mute_volume)
{
    unsigned int flags;
    unsigned int cur;

    flags = params->flags;
    if (flags != 0) {
        if (flags & AKAO_VOICE_PARAM_PITCH) {
            AkaoSpuVoice_SetPitch(voice_index, params->pitch);
            params->flags &= ~AKAO_VOICE_PARAM_PITCH;
            if (params->flags == 0) {
                return;
            }
        }
        if (flags & AKAO_VOICE_PARAM_VOLUME) {
            AkaoSpuVoice_SetVolume(voice_index, mute_volume ? 0 : params->volume_left, mute_volume ? 0 : params->volume_right);
            params->flags &= ~AKAO_VOICE_PARAM_VOLUME;
            if (params->flags == 0) {
                return;
            }
        }
        if (flags & AKAO_VOICE_PARAM_START_ADDR) {
            AkaoSpuVoice_SetStartAddress(voice_index, params->start_address);
            params->flags &= ~AKAO_VOICE_PARAM_START_ADDR;
            if (params->flags == 0) {
                return;
            }
        }
        if (flags & AKAO_VOICE_PARAM_LOOP_ADDR) {
            AkaoSpuVoice_SetRepeatAddress(voice_index, params->loop_address);
            cur = params->flags & ~AKAO_VOICE_PARAM_LOOP_ADDR;
            params->flags = cur;
            if (cur == 0) {
                return;
            }
        }
        if (flags & AKAO_VOICE_PARAM_ADSR_SUSTAIN) {
            AkaoSpuVoice_SetAdsrSustainRate(voice_index, params->adsr_sustain_rate, params->adsr_sustain_mode);
            params->flags &= ~AKAO_VOICE_PARAM_ADSR_SUSTAIN;
            if (params->flags == 0) {
                return;
            }
        }
        if (flags & AKAO_VOICE_PARAM_ADSR_ATTACK) {
            AkaoSpuVoice_SetAdsrAttack(voice_index, params->adsr_attack_rate, params->adsr_attack_mode);
            params->flags &= ~AKAO_VOICE_PARAM_ADSR_ATTACK;
            if (params->flags == 0) {
                return;
            }
        }
        if (flags & AKAO_VOICE_PARAM_ADSR_RELEASE) {
            AkaoSpuVoice_SetAdsrReleaseRate(voice_index, params->adsr_release_rate, params->adsr_release_mode);
            params->flags &= ~AKAO_VOICE_PARAM_ADSR_RELEASE;
            if (params->flags == 0) {
                return;
            }
        }
        if (flags & AKAO_VOICE_PARAM_ADSR_DECAY_SUSTAIN) {
            AkaoSpuVoice_SetAdsrDecayRate(voice_index, params->adsr_decay_rate);
            AkaoSpuVoice_SetAdsrSustainLevel(voice_index, params->adsr_sustain_level);
        }
        params->flags = 0;
    }
}

void Spu_UpdateVoiceRegisters(AkaoTrack *voice, u32 voice_mask) {
    int new_value;
    int value;
    int scaled;
    s16 *table;

    if (voice->expression_duration != 0) {
        u32 old_value;
        old_value = voice->expression_value;
        new_value = old_value + voice->expression_delta;
        voice->expression_duration--;
        if ((new_value & 0xFFE00000) != (old_value & 0xFFE00000)) {
            voice->update_flags |= 3;
        }
        voice->expression_value = new_value;
    }

    if (voice->pitch_slide_duration != 0) {
        voice->pitch_slide_duration--;
        voice->pitch_slide_current += (u16)voice->pitch_slide_delta;
        voice->update_flags |= 3;
    }

    if (voice->volume_duration != 0) {
        u32 old_value;
        old_value = voice->volume_base;
        new_value = old_value + voice->volume_delta;
        voice->volume_duration--;
        if ((new_value & 0x7F00) != (old_value & 0x7F00)) {
            voice->update_flags |= 3;
        }
        voice->volume_base = new_value;
    }

    if (voice->panpot_duration != 0) {
        u32 signed_old;
        signed_old = voice->pan_target;
        new_value = signed_old + voice->pan_delta;
        voice->panpot_duration--;
        if ((voice->flags & 0x100) != 0 &&
            ((new_value & 0xFF00) != (signed_old & 0xFF00))) {
            voice->update_flags |= 3;
        }
        voice->pan_target = new_value;
    }

    if (voice->panpot_slide_duration != 0) {
        u32 old_value;
        old_value = voice->panpot;
        new_value = old_value + voice->panpot_delta;
        voice->panpot_slide_duration--;
        if ((new_value & 0xFF00) != (old_value & 0xFF00)) {
            voice->update_flags |= 3;
        }
        voice->panpot = new_value;
    }

    if (voice->pitch_lfo_counter != 0) {
        voice->pitch_lfo_counter--;
    }

    if (voice->volume_lfo_counter != 0) {
        voice->volume_lfo_counter--;
    }

    if (voice->key_on_delay != 0) {
        voice->key_on_delay--;
        if (voice->key_on_delay == 0) {
            /* Updated through a plain word pointer: a direct member store
             * would let the scheduler move it past the flag update below. */
            AkaoU32 *mask = &D_8009D2C8->bank.key_off_dirty_mask;
            *mask ^= voice_mask;
            g_AkaoVoiceUpdateFlags |= 0x10;
            Seq_MarkTrack34MaskDirty();
        }
    }

    if (voice->key_off_delay != 0) {
        voice->key_off_delay--;
        if (voice->key_off_delay == 0) {
            AkaoU32 *mask = &D_8009D2C8->bank.adsr_dirty_mask;
            *mask ^= voice_mask;
            Seq_MarkTrack3CMaskDirty();
        }
    }

    if (voice->pitch_lfo_slide_duration != 0) {
        voice->pitch_lfo_slide_duration--;
        voice->pitch_lfo_target += (u16)voice->pitch_lfo_delta;
        value = (voice->pitch_lfo_target & 0x7F00) >> 8;
        if ((voice->pitch_lfo_target & 0x8000) != 0) {
            scaled = (value * voice->pitch_base) >> 7;
        } else {
            scaled = (value * (((voice->pitch_base << 4) - voice->pitch_base) >> 8)) >> 7;
        }
        voice->pitch_lfo_depth = scaled;

        if (voice->pitch_lfo_counter == 0 && voice->pitch_lfo_phase != 1) {
            table = voice->pitch_lfo_table;
            if (table[0] == 0 && table[1] == 0) {
                table += table[2];
            }
            new_value = (voice->pitch_lfo_depth * table[0]) >> 16;
            if (new_value != voice->pitch_lfo_value) {
                voice->pitch_lfo_value = new_value;
                voice->update_flags |= 0x10;
                if (new_value >= 0) {
                    voice->pitch_lfo_value = new_value << 1;
                }
            }
        }
    }

    if (voice->volume_lfo_slide_duration != 0) {
        voice->volume_lfo_slide_duration--;
        voice->volume_lfo_target += (u16)voice->volume_lfo_delta;
        if (voice->volume_lfo_counter == 0 && voice->volume_lfo_phase != 1) {
            table = voice->volume_lfo_table;
            if (table[0] == 0 && table[1] == 0) {
                table += table[2];
            }
            { int product = (((s16)(voice->expression_value >> 16) * (voice->volume_base >> 8)) >> 7) * (voice->volume_lfo_target >> 8);
              new_value = (s32)((u32)product << 9) >> 16;
              new_value = (new_value * table[0]) >> 15; }
            if (new_value != voice->volume_lfo_value) {
                voice->volume_lfo_value = new_value;
                voice->update_flags |= 3;
            }
        }
    }

    if (voice->aux_lfo_slide_duration != 0) {
        voice->aux_lfo_slide_duration--;
        voice->aux_lfo_target += (u16)voice->aux_lfo_delta;
        if (voice->aux_lfo_phase != 1) {
            table = voice->aux_lfo_table;
            if (table[0] == 0 && table[1] == 0) {
                table += table[2];
            }
            new_value = ((voice->aux_lfo_target >> 8) * table[0]) >> 15;
            if (new_value != voice->aux_lfo_value) {
                voice->aux_lfo_value = new_value;
                voice->update_flags |= 3;
            }
        }
    }

    if (voice->pitch_slide_steps != 0) {
        u32 old_value;
        old_value = voice->voice_mask_a;
        new_value = old_value + voice->pitch_slide_step;
        voice->pitch_slide_steps--;
        if ((new_value & 0xFFFF0000) != (old_value & 0xFFFF0000)) {
            voice->update_flags |= 0x10;
        }
        voice->voice_mask_a = new_value;
    }
}

void Spu_TickVoiceEnvelopes(AkaoTrack *track, unsigned voice_mask)
{
    int new_value;
    int value;
    u16 depth;
    s16 *table;

    if (track->expression_duration != 0) {
        u32 old_value;
        old_value = track->expression_value;
        new_value = old_value + track->expression_delta;
        track->expression_duration--;
        if ((new_value & 0xFFE00000) != (old_value & 0xFFE00000)) {
            track->update_flags |= 3;
        }
        track->expression_value = new_value;
    }

    if (track->key_on_delay != 0) {
        track->key_on_delay--;
        if (track->key_on_delay == 0) {
            g_AkaoTrack34Mask ^= voice_mask;
            g_AkaoVoiceUpdateFlags |= 0x10;
            Seq_MarkTrack34MaskDirty();
        }
    }

    if (track->key_off_delay != 0) {
        track->key_off_delay--;
        if (track->key_off_delay == 0) {
            g_AkaoTrack3CMask ^= voice_mask;
            Seq_MarkTrack3CMaskDirty();
        }
    }

    if (track->pitch_lfo_slide_duration != 0) {
        track->pitch_lfo_slide_duration--;
        track->pitch_lfo_target += (u16)track->pitch_lfo_delta;
        value = (track->pitch_lfo_target & 0x7F00) >> 8;
        if ((track->pitch_lfo_target & 0x8000) != 0) {
            depth = (value * track->pitch_base) >> 7;
        } else {
            depth = (value * (((track->pitch_base << 4) - track->pitch_base) >> 8)) >> 7;
        }
        track->pitch_lfo_depth = depth;

        if (track->pitch_lfo_phase != 1) {
            table = track->pitch_lfo_table;
            if (table[0] == 0 && table[1] == 0) {
                table += table[2];
            }
            new_value = (track->pitch_lfo_depth * table[0]) >> 16;
            if (new_value != track->pitch_lfo_value) {
                track->pitch_lfo_value = new_value;
                track->update_flags |= 0x10;
                if (new_value >= 0) {
                    track->pitch_lfo_value = new_value << 1;
                }
            }
        }
    }

    if (track->volume_lfo_slide_duration != 0) {
        track->volume_lfo_slide_duration--;
        track->volume_lfo_target += (u16)track->volume_lfo_delta;
        if (track->volume_lfo_phase != 1) {
            table = track->volume_lfo_table;
            if (table[0] == 0 && table[1] == 0) {
                table += table[2];
            }
            {
                int product = (((s16)(track->expression_value >> 16) * (track->volume_base >> 8)) >> 7) *
                              (track->volume_lfo_target >> 8);
                new_value = (s32)((u32)product << 9) >> 16;
                new_value = (new_value * table[0]) >> 15;
            }
            if (new_value != track->volume_lfo_value) {
                track->volume_lfo_value = new_value;
                track->update_flags |= 3;
            }
        }
    }

    if (track->aux_lfo_slide_duration != 0) {
        track->aux_lfo_slide_duration--;
        track->aux_lfo_target += (u16)track->aux_lfo_delta;
        if (track->aux_lfo_phase != 1) {
            table = track->aux_lfo_table;
            if (table[0] == 0 && table[1] == 0) {
                table += table[2];
            }
            new_value = ((track->aux_lfo_target >> 8) * table[0]) >> 15;
            if (new_value != track->aux_lfo_value) {
                track->aux_lfo_value = new_value;
                track->update_flags |= 3;
            }
        }
    }

    if (track->pitch_slide_steps != 0) {
        u32 old_value;
        old_value = track->voice_mask_a;
        new_value = old_value + track->pitch_slide_step;
        track->pitch_slide_steps--;
        if ((new_value & 0xFFFF0000) != (old_value & 0xFFFF0000)) {
            track->update_flags |= 0x10;
        }
        track->voice_mask_a = new_value;
    }
}

void Akao_SetVoiceKeyOff(AkaoTrack *voice, u32 voice_mask, int index) {
    u32 flags;
    int base_volume;
    int value;
    int sample;
    s16 *table;
    AkaoTrack *target;

    base_volume = ((s16)(voice->expression_value >> 16) * (voice->volume_base >> 8)) >> 7;
    flags = voice->flags;

    if ((flags & AKAO_TRACK_FLAG_PITCH_LFO) != 0 && voice->pitch_lfo_counter == 0) {
        if (--voice->pitch_lfo_phase == 0) {
            voice->pitch_lfo_phase = voice->pitch_lfo_duration;
            table = voice->pitch_lfo_table;
            if (table[0] == 0 && table[1] == 0) {
                voice->pitch_lfo_table = table + table[2];
            }
            table = voice->pitch_lfo_table;
            voice->pitch_lfo_table = table + 1;
            sample = *table;
            value = (voice->pitch_lfo_depth * sample) >> 16;
            if (value != voice->pitch_lfo_value) {
                voice->pitch_lfo_value = value;
                voice->update_flags |= AKAO_VOICE_PARAM_PITCH;
                if (value >= 0) {
                    voice->pitch_lfo_value = value << 1;
                }
            }
        }
    }

    if ((flags & AKAO_TRACK_FLAG_VOLUME_LFO) != 0 && voice->volume_lfo_counter == 0) {
        if (--voice->volume_lfo_phase == 0) {
            int product;
            voice->volume_lfo_phase = voice->volume_lfo_duration;
            table = voice->volume_lfo_table;
            if (table[0] == 0 && table[1] == 0) {
                voice->volume_lfo_table = table + table[2];
            }
            product = base_volume * (voice->volume_lfo_target >> 8);
            table = voice->volume_lfo_table;
            voice->volume_lfo_table = table + 1;
            sample = *table;
            value = (s32)((u32)product << 9) >> 16;
            value = (value * sample) >> 15;
            if (value != voice->volume_lfo_value) {
                voice->volume_lfo_value = value;
                voice->update_flags |= AKAO_VOICE_PARAM_VOLUME;
            }
        }
    }

    if ((flags & AKAO_TRACK_FLAG_AUX_LFO) != 0) {
        if (--voice->aux_lfo_phase == 0) {
            voice->aux_lfo_phase = voice->aux_lfo_duration;
            table = voice->aux_lfo_table;
            if (table[0] == 0 && table[1] == 0) {
                voice->aux_lfo_table = table + table[2];
            }
            table = voice->aux_lfo_table;
            voice->aux_lfo_table = table + 1;
            sample = *table;
            value = ((voice->aux_lfo_target >> 8) * sample) >> 15;
            if (value != voice->aux_lfo_value) {
                voice->aux_lfo_value = value;
                voice->update_flags |= AKAO_VOICE_PARAM_VOLUME;
            }
        }
    }

    if ((flags & AKAO_TRACK_FLAG_20) != 0) {
        base_volume = (((s32)((u32)voice[-1].pitch << 17) >> 16) * (voice->volume_base >> 8)) >> 7;
        voice->update_flags |= AKAO_VOICE_PARAM_VOLUME;
    }

    if ((voice->update_flags & AKAO_VOICE_PARAM_VOLUME) != 0) {
        int product;
        base_volume += voice->volume_lfo_value;
        product = base_volume * (D_8009D2C8->timing.pitch_integer & 0x7F);
        value = ((voice->panpot >> 8) + voice->aux_lfo_value) & 0xFF;
        base_volume = product >> 7;
        switch (g_AkaoPlaybackMode) {
        case 1:
            voice->volume_left = (base_volume * g_AkaoPanVolumeTable[value]) >> 15;
            voice->volume_right = (base_volume * g_AkaoPanVolumeTable[value ^ 0xFF]) >> 15;
            break;
        case 4:
            voice->volume_left = (base_volume * g_AkaoPanVolumeTable[value]) >> 15;
            voice->volume_right = (base_volume * g_AkaoPanVolumeTable[value ^ 0xFF]) >> 15;
            if ((voice_mask & 0x00AAAAAA) != 0) {
                voice->volume_right = ~voice->volume_right;
            } else {
                voice->volume_left = ~voice->volume_left;
            }
            break;
        default:
            voice->volume_right = (unsigned int)(base_volume * g_AkaoPanVolumeTable[0x80]) >> 15;
            voice->volume_left = voice->volume_right;
            break;
        }

        if ((flags & AKAO_TRACK_FLAG_VOICE_ALLOCATED) != 0) {
            target = &g_AkaoSequencerTracks[voice->voice_index].track;
            target->volume_left = voice->volume_left;
            target->volume_right = voice->volume_right;
            base_volume = voice->pitch_slide_current >> 1;
            switch (g_AkaoPlaybackMode) {
        case 1:
            voice->volume_left = (base_volume * g_AkaoPanVolumeTable[value]) >> 15;
            voice->volume_right = (base_volume * g_AkaoPanVolumeTable[value ^ 0xFF]) >> 15;
            break;
        case 4:
            voice->volume_left = (base_volume * g_AkaoPanVolumeTable[value]) >> 15;
            voice->volume_right = (base_volume * g_AkaoPanVolumeTable[value ^ 0xFF]) >> 15;
            if ((voice_mask & 0x00AAAAAA) != 0) {
                voice->volume_right = ~voice->volume_right;
            } else {
                voice->volume_left = ~voice->volume_left;
            }
            break;
        default:
            voice->volume_right = (unsigned int)(base_volume * g_AkaoPanVolumeTable[0x80]) >> 15;
            voice->volume_left = voice->volume_right;
            break;
        }
            target->volume_left -= voice->volume_left;
            target->volume_right -= voice->volume_right;
        }
    }

    if ((flags & AKAO_TRACK_FLAG_10) != 0) {
        int pitch;
        value = g_AkaoGlobalPitchScale;
        pitch = voice[-1].pitch + voice->pitch_lfo_value + (s16)(voice->voice_mask_a >> 16);
        if (value != 0) {
            if (value < 0x80) {
                pitch += (pitch * value) >> 7;
            } else {
                pitch = (pitch * value) >> 8;
            }
        }
        voice->pitch = pitch & 0x3FFF;
        voice->update_flags |= AKAO_VOICE_PARAM_PITCH;
    } else if ((voice->update_flags & AKAO_VOICE_PARAM_PITCH) != 0) {
        int pitch;
        value = g_AkaoGlobalPitchScale;
        pitch = voice->pitch_base + voice->pitch_lfo_value + (s16)(voice->voice_mask_a >> 16);
        if (value != 0) {
            if (value < 0x80) {
                pitch += (pitch * value) >> 7;
            } else {
                pitch = (pitch * value) >> 8;
            }
        }
        voice->pitch = pitch & 0x3FFF;
    }
}

void Akao_SetVoiceKeyOn(AkaoTrack *voice, u32 voice_mask) {
    u32 flags;
    int base_volume;
    /* Two scratch values cover every stage, as in the retail use:
     * value holds LFO results, the pan index and the pitch depth, operand
     * holds the wave sample and the pitch sum. */
    int value;
    int operand;
    s16 *table;

    base_volume = ((s16)(voice->expression_value >> 16) * (voice->volume_base >> 8)) >> 7;
    flags = voice->flags;

    if ((flags & AKAO_TRACK_FLAG_PITCH_LFO) != 0) {
        if (--voice->pitch_lfo_phase == 0) {
            voice->pitch_lfo_phase = voice->pitch_lfo_duration;
            table = voice->pitch_lfo_table;
            if (table[0] == 0 && table[1] == 0) {
                voice->pitch_lfo_table = table + table[2];
            }
            table = voice->pitch_lfo_table;
            voice->pitch_lfo_table = table + 1;
            operand = *table;
            value = (voice->pitch_lfo_depth * operand) >> 16;
            if (value != voice->pitch_lfo_value) {
                voice->pitch_lfo_value = value;
                voice->update_flags |= AKAO_VOICE_PARAM_PITCH;
                if (value >= 0) {
                    voice->pitch_lfo_value = value << 1;
                }
            }
        }
    }

    if ((flags & AKAO_TRACK_FLAG_VOLUME_LFO) != 0) {
        if (--voice->volume_lfo_phase == 0) {
            int product;
            voice->volume_lfo_phase = voice->volume_lfo_duration;
            table = voice->volume_lfo_table;
            if (table[0] == 0 && table[1] == 0) {
                voice->volume_lfo_table = table + table[2];
            }
            product = base_volume * (voice->volume_lfo_target >> 8);
            table = voice->volume_lfo_table;
            voice->volume_lfo_table = table + 1;
            operand = *table;
            value = (s32)((u32)product << 9) >> 16;
            value = (value * operand) >> 15;
            if (value != voice->volume_lfo_value) {
                voice->volume_lfo_value = value;
                voice->update_flags |= AKAO_VOICE_PARAM_VOLUME;
            }
        }
    }

    if ((flags & AKAO_TRACK_FLAG_AUX_LFO) != 0) {
        if (--voice->aux_lfo_phase == 0) {
            voice->aux_lfo_phase = voice->aux_lfo_duration;
            table = voice->aux_lfo_table;
            if (table[0] == 0 && table[1] == 0) {
                voice->aux_lfo_table = table + table[2];
            }
            table = voice->aux_lfo_table;
            voice->aux_lfo_table = table + 1;
            operand = *table;
            value = ((voice->aux_lfo_target >> 8) * operand) >> 15;
            if (value != voice->aux_lfo_value) {
                voice->aux_lfo_value = value;
                voice->update_flags |= AKAO_VOICE_PARAM_VOLUME;
            }
        }
    }

    if ((flags & AKAO_TRACK_FLAG_20) != 0) {
        base_volume = (((s32)((u32)voice[-1].pitch << 17) >> 16) * (voice->volume_base >> 8)) >> 7;
        voice->update_flags |= AKAO_VOICE_PARAM_VOLUME;
    }

    if ((voice->update_flags & AKAO_VOICE_PARAM_VOLUME) != 0) {
        base_volume += voice->volume_lfo_value;
        if ((voice->key_on_mask & 0x02000000) != 0) {
            value = 0x80;
        } else {
            base_volume = (base_volume * (s8)(voice->pan_target >> 8)) >> 7;
            value = ((voice->panpot >> 8) + voice->aux_lfo_value) & 0xFF;
        }

        switch (g_AkaoPlaybackMode) {
        case 1:
            voice->volume_left = (base_volume * g_AkaoPanVolumeTable[value]) >> 15;
            voice->volume_right = (base_volume * g_AkaoPanVolumeTable[value ^ 0xFF]) >> 15;
            break;
        case 4:
            voice->volume_left = (base_volume * g_AkaoPanVolumeTable[value]) >> 15;
            voice->volume_right = (base_volume * g_AkaoPanVolumeTable[value ^ 0xFF]) >> 15;
            if ((voice_mask & 0x00AAAAAA) != 0) {
                voice->volume_right = ~voice->volume_right;
            } else {
                voice->volume_left = ~voice->volume_left;
            }
            break;
        default:
            voice->volume_right = (unsigned int)(base_volume * g_AkaoPanVolumeTable[0x80]) >> 15;
            voice->volume_left = voice->volume_right;
            break;
        }
    }

    if ((flags & AKAO_TRACK_FLAG_10) != 0) {
        operand = voice[-1].pitch + voice->pitch_lfo_value + (s16)(voice->voice_mask_a >> 16);
        if ((voice->key_on_mask & 0x02000000) == 0) {
            value = (u8)(voice->pitch_offset >> 8);
            if (value != 0) {
                if (value < 0x80) {
                    operand += (operand * value) >> 7;
                } else {
                    operand = (operand * value) >> 8;
                }
            }
        }
        voice->pitch = operand & 0x3FFF;
        voice->update_flags |= AKAO_VOICE_PARAM_PITCH;
    } else if ((voice->update_flags & AKAO_VOICE_PARAM_PITCH) != 0) {
        operand = voice->pitch_base + voice->pitch_lfo_value + (s16)(voice->voice_mask_a >> 16);
        if ((voice->key_on_mask & 0x02000000) == 0) {
            value = (u8)(voice->pitch_offset >> 8);
            if (value != 0) {
                if (value < 0x80) {
                    operand += (operand * value) >> 7;
                } else {
                    operand = (operand * value) >> 8;
                }
            }
        }
        voice->pitch = operand & 0x3FFF;
    }
}

void Spu_CopyVoiceToStereoSlot(AkaoTrack *track, int stereo_voice_index) {
    int left;
    int right;
    int pan;
    int new_var;
    int inverse_pan;
    AkaoTrack *stereo;

    left = track->volume_left;
    inverse_pan = 0x7F;
    pan = (unsigned short)track->pan_target;
    pan = ((pan << 16)) >> 24;
    inverse_pan -= pan;
    track->volume_left = ((unsigned int)(left * inverse_pan)) >> 8;
    stereo = &g_AkaoVoiceStateTable[stereo_voice_index];
    new_var = inverse_pan;
    stereo->volume_left = (left * (track->pan_target)) >> 16;
    right = (left = track->volume_right);
    track->volume_right = ((unsigned int)(right * new_var)) >> 8;
    stereo->volume_right = (right * (track->pan_target)) >> 16;
    stereo->pitch = track->pitch;
    stereo->update_flags |= (*track).update_flags;
    write_voice_param(track->assigned_voice_index, &AKAO_TRACK_VOICE(track), s_music_muted);
    write_voice_param(stereo_voice_index, &AKAO_TRACK_VOICE(stereo), s_music_muted);
}

void Spu_RestoreVoiceFromStereoSlot(AkaoTrack *track, int stereo_voice_index) {
    AkaoTrack *stereo;
    AkaoS16 right;

    write_voice_param(track->assigned_voice_index, &AKAO_TRACK_VOICE(track), s_music_muted);
    stereo = &g_AkaoVoiceStateTable[stereo_voice_index];
    track->volume_left = stereo->volume_left;
    right = stereo->volume_right;
    track->update_flags |= 0x1FF93;
    track->volume_right = right;
    write_voice_param(stereo_voice_index, &AKAO_TRACK_VOICE(track), s_music_muted);
}

/* SFX and other native callers use their ordinary, unmuted voice path. */
void Akao_WriteVoiceParam(int voice_index, AkaoVoiceParams *params)
{
    write_voice_param(voice_index, params, 0);
}

void Akao_StepVoiceNote(AkaoTrack *track, unsigned mask, unsigned direct, unsigned *output) {
    unsigned bit = 1;
    int index = 0;
    unsigned requested = mask & g_AkaoCurTrack->key_on_request_mask;
    do {
        if (mask & bit) {
            Akao_SetVoiceKeyOff(track, bit, index);
            if (track->update_flags) {
                if (requested & bit) {
                    if (direct & bit) {
                        *output |= 1 << index;
                        track->assigned_voice_index = index;
                        track->update_flags |= 0x1ff93;
                    } else {
                        unsigned voice = 0;
                        AkaoVoiceEnvelopeSlot *slot = g_AkaoVoiceEnvelopeTable;
                        do {
                            if (!slot->level) {
                                track->update_flags |= 0x1ff93;
                                *output |= 1 << voice;
                                track->assigned_voice_index = voice;
                                *(short *)&slot->level = 0x7fff;
                                g_AkaoVoiceUpdateFlags |= 0x100;
                                voice = 24;
                            } else {
                                voice++;
                                slot++;
                                if (voice == 24) {
                                    track->assigned_voice_index = voice;
                                    g_AkaoCurTrack->status_flags |= 1;
                                }
                            }
                        } while (voice < 24);
                    }
                }
                if (g_AkaoVoicePortamentoResetMask & bit) {
                    track->volume_right = 0;
                    track->volume_left = 0;
                }
                if ((unsigned)track->assigned_voice_index < 24)
                    write_voice_param(track->assigned_voice_index, &AKAO_TRACK_VOICE(track), s_music_muted);
            }
            mask &= ~bit;
        }
        bit <<= 1;
        track++;
        index++;
    } while (mask);
}

/* Release the assigned voice in all 24 native track records. */
void Akao_RemoveVoice(AkaoTrack *voices, int voiceIndex) {
    for(unsigned i=0;i<AKAO_VOICE_COUNT;++i)
        if(voices[i].assigned_voice_index==voiceIndex)
            voices[i].assigned_voice_index=AKAO_VOICE_COUNT;
}

void Akao_UpdateVoiceEnvelopes(s32 protectedMask) {
    AkaoVoiceEnvelopeSlot *envelope;
    s32 combinedMask;
    u32 voiceIndex;
    u32 mask;
    AkaoTrack *voiceBase;

    voiceIndex = 0;
    mask = 1;
    voiceBase = &g_AkaoVoiceStateTable[0];
    envelope = &g_AkaoVoiceEnvelopeTable[0];
    combinedMask = ((g_AkaoCurTrack[0].active_voice_mask & g_AkaoCurTrack[0].yield_voice_mask) | (g_AkaoCurTrack[1].active_voice_mask & g_AkaoCurTrack[1].yield_voice_mask)) | protectedMask;
    do {
        if (combinedMask & (mask << voiceIndex)) {
            s32 level = 0x7FFF;
            envelope->level = level;
        } else {
            SpuGetVoiceEnvelope(voiceIndex, (short *)envelope);
            if (envelope->level == 0) {
                if (envelope) {
                    Akao_RemoveVoice(voiceBase, voiceIndex);
                    Akao_RemoveVoice(voiceBase + AKAO_VOICE_COUNT, voiceIndex);
                } else {
                    Akao_RemoveVoice(voiceBase, voiceIndex);
                    Akao_RemoveVoice(voiceBase + AKAO_VOICE_COUNT, voiceIndex);
                }
            }
        }
        voiceIndex += 1;
        envelope++;
    } while (voiceIndex < 0x18U);
}
void Akao_ProcessVoiceQueue(void)
{
  unsigned *active = &g_SpuActiveVoiceMask;
  unsigned excluded;
  unsigned secondary;
  unsigned secondary_new;
  unsigned int allocated_primary;
  unsigned primary;
  unsigned primary_new;
  unsigned result = 0;

  unsigned bit;
  unsigned flags;
  AkaoSequencerBank *bank;
  AkaoTrack *track;
  excluded = (*active) | g_SpuStoppedVoiceMask;
  if ((g_AkaoCurTrack[0].active_voice_mask & g_AkaoCurTrack[0].key_on_request_mask) | (g_AkaoCurTrack[1].active_voice_mask & g_AkaoCurTrack[1].key_on_request_mask))
  {
    Akao_UpdateVoiceEnvelopes(excluded);
  }
  secondary = (g_AkaoCurTrack[1].active_voice_mask & g_AkaoCurTrack[1].allocated_voice_mask) & (~(g_AkaoCurTrack[1].yield_voice_mask & excluded));
  secondary_new = (secondary & g_AkaoCurTrack[1].yield_voice_mask) & (~excluded);
  if (secondary & g_AkaoCurTrack[1].pending_voice_mask)
  {
    g_AkaoCurTrack++;
    Akao_StepVoiceNote(g_AkaoVoiceStateTable2, secondary & g_AkaoCurTrack->pending_voice_mask, secondary_new, &result);
    bank = g_AkaoCurTrack;
    secondary &= ~bank->pending_voice_mask;
    g_AkaoCurTrack--;
    bank->key_on_request_mask &= ~bank->pending_voice_mask;
  }
  allocated_primary = g_AkaoCurTrack->active_voice_mask & g_AkaoCurTrack->allocated_voice_mask;
  primary_new = secondary_new | excluded;
  primary = allocated_primary & (~(g_AkaoCurTrack->yield_voice_mask & primary_new));
  excluded = (primary & g_AkaoCurTrack->yield_voice_mask) & (~primary_new);
  if (primary & g_AkaoCurTrack->pending_voice_mask)
  {
    Akao_StepVoiceNote(g_AkaoVoiceStateTable, primary & g_AkaoCurTrack->pending_voice_mask, excluded, &result);
    bank = g_AkaoCurTrack;
    bank->key_on_request_mask &= ~bank->pending_voice_mask;
    primary &= ~bank->pending_voice_mask;
  }
  if (secondary)
  {
    g_AkaoCurTrack++;
    Akao_StepVoiceNote(g_AkaoVoiceStateTable2, secondary, secondary_new & (~excluded), &result);
    g_AkaoCurTrack->key_on_request_mask = 0;
    g_AkaoCurTrack--;
  }
  if (primary)
  {
    Akao_StepVoiceNote(g_AkaoVoiceStateTable, primary, excluded, &result);
    g_AkaoCurTrack->key_on_request_mask = 0;
  }
  primary = (*active) & g_AkaoSfxAllocatedVoiceMask;
  if (primary)
  {
    bit = 0x1000;
    track = g_AkaoVoiceChannelTable;
    result |= g_AkaoVoiceMaskScratch;
    while (primary)
    {
      if (primary & bit)
      {
        Akao_SetVoiceKeyOn(track, bit);
        if (track->update_flags)
        {
          Akao_WriteVoiceParam(track->assigned_voice_index, &AKAO_TRACK_VOICE(track));
        }
        secondary_new = bit;
        primary &= ~secondary_new;
      }
      bit <<= 1;
      track++;
    }

    g_AkaoVoiceMaskScratch = 0;
  }
  primary = g_AkaoVoiceUpdateFlags;
  if (primary & 0x80)
  {
    short volume = ((short *) &g_AkaoCurTrack->volume)[1];
    SpuSetReverbModeDepth(volume, volume);
    g_AkaoVoiceUpdateFlags &= ~0x80;
  }
  if (primary & 0x10)
  {
    unsigned short clock;
    if (g_SpuActiveVoiceMask)
    {
      clock = g_AkaoTrack5ATransposeValue;
    }
    else
    {
      clock = g_AkaoCurTrack->noise_clock;
    }
    SpuSetNoiseClock(clock);
    g_AkaoVoiceUpdateFlags &= ~0x10;
  }
  if (0x100 & primary)
  {
    Akao_SetVoiceAdsr();
    Akao_SetVoiceVolume();
    Akao_SetVoiceStartAddr();
    Spu_WriteReverbEnable(g_AkaoReverbEnableMask);
    Spu_WriteNoiseEnable(g_AkaoNoiseEnableMask);
    Spu_WriteFmEnable(g_AkaoFmEnableMask);
    g_AkaoVoiceUpdateFlags &= ~0x100;
  }
  if (result)
  {
    Spu_WriteKeyOn(result);
  }
}

void Spu_VoiceMaskCompose(AkaoTrack *track, int *mask_out, int mask, int mask_keep) {
    int bit;
    int idx;

    bit = 1;
    do {
        if ((mask & bit) != 0) {
            idx = track->assigned_voice_index;
            if ((unsigned int) idx < 0x18) {
                *mask_out |= 1 << idx;
            }
        }
        mask &= ~bit;
        track++;
        bit <<= 1;
    } while (mask != 0);

    *mask_out &= mask_keep;
}

void Akao_SetVoicePitch(void) {
    unsigned secondary, primary, keep;
    AkaoSequencerBank *bank;
    int result = 0;
    keep = ~(g_SpuActiveVoiceMask | g_SpuStoppedVoiceMask);
    secondary = g_AkaoCurTrack[1].active_voice_mask & g_AkaoCurTrack[1].key_off_request_mask;
    if (secondary & g_AkaoCurTrack[1].pending_voice_mask) {
        g_AkaoCurTrack++;
        Spu_VoiceMaskCompose(g_AkaoVoiceStateTable2, &result, secondary & g_AkaoCurTrack->pending_voice_mask, keep);
        bank = g_AkaoCurTrack;
        secondary &= ~bank->pending_voice_mask;
        g_AkaoCurTrack--;
        bank->key_off_request_mask &= ~bank->pending_voice_mask;
    }
    primary = g_AkaoCurTrack->active_voice_mask & g_AkaoCurTrack->key_off_request_mask;
    if (primary & g_AkaoCurTrack->pending_voice_mask) {
        Spu_VoiceMaskCompose(g_AkaoVoiceStateTable, &result, primary & g_AkaoCurTrack->pending_voice_mask, keep);
        bank = g_AkaoCurTrack;
        bank->key_off_request_mask &= ~bank->pending_voice_mask;
        primary &= ~bank->pending_voice_mask;
    }
    if (secondary) {
        g_AkaoCurTrack++;
        Spu_VoiceMaskCompose(g_AkaoVoiceStateTable2, &result, secondary, keep);
        g_AkaoCurTrack->key_off_request_mask = 0;
        g_AkaoCurTrack--;
    }
    if (primary) {
        Spu_VoiceMaskCompose(g_AkaoVoiceStateTable, &result, primary, keep);
        g_AkaoCurTrack->key_off_request_mask = 0;
    }
    result |= g_SpuPendingKeyOffMask;
    g_SpuPendingKeyOffMask = 0;
    if (result) Spu_WriteKeyOff(result);
}

void Seq_MarkTrack34MaskDirty(void) {
    g_AkaoVoiceUpdateFlags |= 0x100;
}

void Akao_SetVoiceVolume(void) {
    unsigned secondary, primary, keep;
    int result = 0;
    keep = ~(g_SpuActiveVoiceMask | g_SpuStoppedVoiceMask);
    secondary = g_AkaoCurTrack[1].active_voice_mask & g_AkaoCurTrack[1].key_off_dirty_mask;
    if (secondary & g_AkaoCurTrack[1].pending_voice_mask) {
        g_AkaoCurTrack++;
        Spu_VoiceMaskCompose(g_AkaoVoiceStateTable2, &result, secondary & g_AkaoCurTrack->pending_voice_mask, keep);
        secondary &= ~g_AkaoCurTrack->pending_voice_mask;
        g_AkaoCurTrack--;
    }
    primary = g_AkaoCurTrack->active_voice_mask & g_AkaoCurTrack->key_off_dirty_mask;
    if (primary & g_AkaoCurTrack->pending_voice_mask) {
        Spu_VoiceMaskCompose(g_AkaoVoiceStateTable, &result, primary & g_AkaoCurTrack->pending_voice_mask, keep);
        primary &= ~g_AkaoCurTrack->pending_voice_mask;
    }
    if (secondary) {
        g_AkaoCurTrack++;
        Spu_VoiceMaskCompose(g_AkaoVoiceStateTable2, &result, secondary, keep);
        g_AkaoCurTrack--;
    }
    if (primary) Spu_VoiceMaskCompose(g_AkaoVoiceStateTable, &result, primary, keep);
    result |= g_AkaoTrack34Mask;
    g_AkaoNoiseEnableMask = result;
    g_AkaoVoiceUpdateFlags |= 0x100;
}

void Seq_MarkTrack38MaskDirty(void) {
    g_AkaoVoiceUpdateFlags |= 0x100;
}

void Akao_SetVoiceAdsr(void) {
    unsigned secondary, primary, keep;
    int result = 0;
    keep = ~(g_SpuActiveVoiceMask | g_SpuStoppedVoiceMask);
    secondary = g_AkaoCurTrack[1].active_voice_mask & g_AkaoCurTrack[1].volume_dirty_mask;
    if (secondary & g_AkaoCurTrack[1].pending_voice_mask) {
        g_AkaoCurTrack++;
        Spu_VoiceMaskCompose(g_AkaoVoiceStateTable2, &result, secondary & g_AkaoCurTrack->pending_voice_mask, keep);
        secondary &= ~g_AkaoCurTrack->pending_voice_mask;
        g_AkaoCurTrack--;
    }
    primary = g_AkaoCurTrack->active_voice_mask & g_AkaoCurTrack->volume_dirty_mask;
    if (primary & g_AkaoCurTrack->pending_voice_mask) {
        Spu_VoiceMaskCompose(g_AkaoVoiceStateTable, &result, primary & g_AkaoCurTrack->pending_voice_mask, keep);
        primary &= ~g_AkaoCurTrack->pending_voice_mask;
    }
    if (secondary) {
        g_AkaoCurTrack++;
        Spu_VoiceMaskCompose(g_AkaoVoiceStateTable2, &result, secondary, keep);
        g_AkaoCurTrack--;
    }
    if (primary) Spu_VoiceMaskCompose(g_AkaoVoiceStateTable, &result, primary, keep);
    result |= g_AkaoTrack38Mask;
    g_AkaoReverbEnableMask = result;
    g_AkaoVoiceUpdateFlags |= 0x100;
}

void Seq_MarkTrack3CMaskDirty(void) {
    g_AkaoVoiceUpdateFlags |= 0x100;
}

void Akao_SetVoiceStartAddr(void) {
    unsigned secondary, primary, keep;
    int result = 0;
    keep = ~(g_SpuActiveVoiceMask | g_SpuStoppedVoiceMask);
    secondary = g_AkaoCurTrack[1].active_voice_mask & g_AkaoCurTrack[1].adsr_dirty_mask;
    if (secondary & g_AkaoCurTrack[1].pending_voice_mask) {
        g_AkaoCurTrack++;
        Spu_VoiceMaskCompose(g_AkaoVoiceStateTable2, &result, secondary & g_AkaoCurTrack->pending_voice_mask, keep);
        secondary &= ~g_AkaoCurTrack->pending_voice_mask;
        g_AkaoCurTrack--;
    }
    primary = g_AkaoCurTrack->active_voice_mask & g_AkaoCurTrack->adsr_dirty_mask;
    if (primary & g_AkaoCurTrack->pending_voice_mask) {
        Spu_VoiceMaskCompose(g_AkaoVoiceStateTable, &result, primary & g_AkaoCurTrack->pending_voice_mask, keep);
        primary &= ~g_AkaoCurTrack->pending_voice_mask;
    }
    if (secondary) {
        g_AkaoCurTrack++;
        Spu_VoiceMaskCompose(g_AkaoVoiceStateTable2, &result, secondary, keep);
        g_AkaoCurTrack--;
    }
    if (primary) Spu_VoiceMaskCompose(g_AkaoVoiceStateTable, &result, primary, keep);
    result |= g_AkaoTrack3CMask;
    g_AkaoFmEnableMask = result;
    g_AkaoVoiceUpdateFlags |= 0x100;
}

/* Runtime storage for asset-backed original driver inputs. */
unsigned short *g_AkaoSampleOffsetTable;
char *g_AkaoSampleDataBase;
AkaoStagedCommand g_AkaoCmd;
AkaoSequenceCounter g_AkaoCmd24Sequence = {0x400};
AkaoQueueEntry g_AkaoMessageQueue[16];
int g_AkaoMessageQueueCount;
volatile u32 g_AkaoMessageQueueLock;
/* Complete authored USA message dispatch table (0x8009C0C0). */
AkaoMessageHandler Akao_MessageHandlers[256] = {
    Akao_MessageNoop, /* 0x00 */
    Akao_MessageNoop, /* 0x01 */
    Akao_MessageNoop, /* 0x02 */
    Akao_MessageNoop, /* 0x03 */
    Akao_MessageNoop, /* 0x04 */
    Akao_MessageNoop, /* 0x05 */
    Akao_MessageNoop, /* 0x06 */
    Akao_MessageNoop, /* 0x07 */
    Akao_MessageNoop, /* 0x08 */
    Akao_MessageNoop, /* 0x09 */
    Akao_MessageNoop, /* 0x0a */
    Akao_MessageNoop, /* 0x0b */
    Akao_MessageNoop, /* 0x0c */
    Akao_MessageNoop, /* 0x0d */
    Akao_MessageNoop, /* 0x0e */
    Akao_MessageNoop, /* 0x0f */
    Seq_SelectPlaybackBank, /* 0x10 */
    Akao_InitPrimarySecondaryVoicesWithMode, /* 0x11 */
    Seq_SelectPlaybackBankWithCountdown, /* 0x12 */
    Akao_MessageNoop, /* 0x13 */
    Akao_MessageNoop, /* 0x14 */
    Akao_MessageNoop, /* 0x15 */
    Akao_MessageNoop, /* 0x16 */
    Akao_MessageNoop, /* 0x17 */
    Akao_MessageNoop, /* 0x18 */
    Seq_RestoreSecondaryStateAndSelect, /* 0x19 */
    Akao_MessageNoop, /* 0x1a */
    Akao_MessageNoop, /* 0x1b */
    Akao_MessageNoop, /* 0x1c */
    Akao_MessageNoop, /* 0x1d */
    Akao_MessageNoop, /* 0x1e */
    Akao_MessageNoop, /* 0x1f */
    Seq_StartIndexedNestedStream, /* 0x20 */
    Spu_ManageVoicesCmd, /* 0x21 */
    Akao_MessageNoop, /* 0x22 */
    Akao_MessageNoop, /* 0x23 */
    Seq_StartRelativeNestedStream, /* 0x24 */
    Akao_MessageNoop, /* 0x25 */
    Akao_MessageNoop, /* 0x26 */
    Akao_MessageNoop, /* 0x27 */
    Akao_MessageNoop, /* 0x28 */
    Akao_MessageNoop, /* 0x29 */
    Akao_MessageNoop, /* 0x2a */
    Akao_MessageNoop, /* 0x2b */
    Akao_MessageNoop, /* 0x2c */
    Akao_MessageNoop, /* 0x2d */
    Akao_MessageNoop, /* 0x2e */
    Akao_MessageNoop, /* 0x2f */
    Seq_StartIndexedNestedStreamWithDefaults, /* 0x30 */
    Akao_MessageNoop, /* 0x31 */
    Akao_MessageNoop, /* 0x32 */
    Akao_MessageNoop, /* 0x33 */
    Seq_StartDefaultNestedStream, /* 0x34 */
    Akao_MessageNoop, /* 0x35 */
    Akao_MessageNoop, /* 0x36 */
    Akao_MessageNoop, /* 0x37 */
    Akao_MessageNoop, /* 0x38 */
    Akao_MessageNoop, /* 0x39 */
    Akao_MessageNoop, /* 0x3a */
    Akao_MessageNoop, /* 0x3b */
    Akao_MessageNoop, /* 0x3c */
    Akao_MessageNoop, /* 0x3d */
    Akao_MessageNoop, /* 0x3e */
    Akao_MessageNoop, /* 0x3f */
    Seq_RestorePrimaryState, /* 0x40 */
    Akao_MessageNoop, /* 0x41 */
    Akao_MessageNoop, /* 0x42 */
    Akao_MessageNoop, /* 0x43 */
    Akao_MessageNoop, /* 0x44 */
    Akao_MessageNoop, /* 0x45 */
    Akao_MessageNoop, /* 0x46 */
    Akao_MessageNoop, /* 0x47 */
    Akao_MessageNoop, /* 0x48 */
    Akao_MessageNoop, /* 0x49 */
    Akao_MessageNoop, /* 0x4a */
    Akao_MessageNoop, /* 0x4b */
    Akao_MessageNoop, /* 0x4c */
    Akao_MessageNoop, /* 0x4d */
    Akao_MessageNoop, /* 0x4e */
    Akao_MessageNoop, /* 0x4f */
    Akao_MessageNoop, /* 0x50 */
    Akao_MessageNoop, /* 0x51 */
    Akao_MessageNoop, /* 0x52 */
    Akao_MessageNoop, /* 0x53 */
    Akao_MessageNoop, /* 0x54 */
    Akao_MessageNoop, /* 0x55 */
    Akao_MessageNoop, /* 0x56 */
    Akao_MessageNoop, /* 0x57 */
    Akao_MessageNoop, /* 0x58 */
    Akao_MessageNoop, /* 0x59 */
    Akao_MessageNoop, /* 0x5a */
    Akao_MessageNoop, /* 0x5b */
    Akao_MessageNoop, /* 0x5c */
    Akao_MessageNoop, /* 0x5d */
    Akao_MessageNoop, /* 0x5e */
    Akao_MessageNoop, /* 0x5f */
    Akao_MessageNoop, /* 0x60 */
    Akao_MessageNoop, /* 0x61 */
    Akao_MessageNoop, /* 0x62 */
    Akao_MessageNoop, /* 0x63 */
    Akao_MessageNoop, /* 0x64 */
    Akao_MessageNoop, /* 0x65 */
    Akao_MessageNoop, /* 0x66 */
    Akao_MessageNoop, /* 0x67 */
    Akao_MessageNoop, /* 0x68 */
    Akao_MessageNoop, /* 0x69 */
    Akao_MessageNoop, /* 0x6a */
    Akao_MessageNoop, /* 0x6b */
    Akao_MessageNoop, /* 0x6c */
    Akao_MessageNoop, /* 0x6d */
    Akao_MessageNoop, /* 0x6e */
    Akao_MessageNoop, /* 0x6f */
    Akao_MessageNoop, /* 0x70 */
    Akao_MessageNoop, /* 0x71 */
    Akao_MessageNoop, /* 0x72 */
    Akao_MessageNoop, /* 0x73 */
    Akao_MessageNoop, /* 0x74 */
    Akao_MessageNoop, /* 0x75 */
    Akao_MessageNoop, /* 0x76 */
    Akao_MessageNoop, /* 0x77 */
    Akao_MessageNoop, /* 0x78 */
    Akao_MessageNoop, /* 0x79 */
    Akao_MessageNoop, /* 0x7a */
    Akao_MessageNoop, /* 0x7b */
    Akao_MessageNoop, /* 0x7c */
    Akao_MessageNoop, /* 0x7d */
    Akao_MessageNoop, /* 0x7e */
    Akao_MessageNoop, /* 0x7f */
    Seq_SetPlaybackMode1AndRefreshVoices, /* 0x80 */
    Seq_SetPlaybackMode2AndRefreshVoices, /* 0x81 */
    Seq_SetPlaybackMode4AndRefreshVoices, /* 0x82 */
    Akao_MessageNoop, /* 0x83 */
    Akao_MessageNoop, /* 0x84 */
    Akao_MessageNoop, /* 0x85 */
    Akao_MessageNoop, /* 0x86 */
    Akao_MessageNoop, /* 0x87 */
    Akao_MessageNoop, /* 0x88 */
    Akao_MessageNoop, /* 0x89 */
    Akao_MessageNoop, /* 0x8a */
    Akao_MessageNoop, /* 0x8b */
    Akao_MessageNoop, /* 0x8c */
    Akao_MessageNoop, /* 0x8d */
    Akao_MessageNoop, /* 0x8e */
    Akao_MessageNoop, /* 0x8f */
    Seq_SetGlobalD2B8AndDirtyAllTracks, /* 0x90 */
    Akao_MessageNoop, /* 0x91 */
    Seq_SetCurrentTrackField56, /* 0x92 */
    Akao_MessageNoop, /* 0x93 */
    Akao_MessageNoop, /* 0x94 */
    Akao_MessageNoop, /* 0x95 */
    Akao_MessageNoop, /* 0x96 */
    Akao_MessageNoop, /* 0x97 */
    Akao_MessageNoop, /* 0x98 */
    Akao_MessageNoop, /* 0x99 */
    Seq_RestorePendingTracks, /* 0x9a */
    Seq_DeactivatePendingTracks, /* 0x9b */
    Spu_RestoreStoppedVoices, /* 0x9c */
    Spu_VoiceStopAll, /* 0x9d */
    Akao_MessageNoop, /* 0x9e */
    Akao_MessageNoop, /* 0x9f */
    Spu_SetVoiceVolumeImmediateMasked, /* 0xa0 */
    Spu_ParameterSlide, /* 0xa1 */
    Spu_SetVoicePanImmediateMasked, /* 0xa2 */
    Spu_SlideVoicePanMasked, /* 0xa3 */
    Spu_SetVoicePitchImmediateMasked, /* 0xa4 */
    Spu_SlideVoicePitchMasked, /* 0xa5 */
    Akao_MessageNoop, /* 0xa6 */
    Akao_MessageNoop, /* 0xa7 */
    Spu_SetAllVoiceVolumeImmediate, /* 0xa8 */
    Spu_SlideAllVoiceVolume, /* 0xa9 */
    Spu_SetAllVoicePanImmediate, /* 0xaa */
    Spu_SlideAllVoicePan, /* 0xab */
    Spu_SetAllVoicePitchImmediate, /* 0xac */
    Spu_SlideAllVoicePitch, /* 0xad */
    Akao_MessageNoop, /* 0xae */
    Akao_MessageNoop, /* 0xaf */
    Akao_MessageNoop, /* 0xb0 */
    Akao_MessageNoop, /* 0xb1 */
    Akao_MessageNoop, /* 0xb2 */
    Akao_MessageNoop, /* 0xb3 */
    Akao_MessageNoop, /* 0xb4 */
    Akao_MessageNoop, /* 0xb5 */
    Akao_MessageNoop, /* 0xb6 */
    Akao_MessageNoop, /* 0xb7 */
    Akao_MessageNoop, /* 0xb8 */
    Akao_MessageNoop, /* 0xb9 */
    Akao_MessageNoop, /* 0xba */
    Akao_MessageNoop, /* 0xbb */
    Akao_MessageNoop, /* 0xbc */
    Akao_MessageNoop, /* 0xbd */
    Akao_MessageNoop, /* 0xbe */
    Akao_MessageNoop, /* 0xbf */
    Seq_SetTrackPitchImmediate, /* 0xc0 */
    Seq_SlideTrackPitch, /* 0xc1 */
    Seq_TrackPitchSetup, /* 0xc2 */
    Akao_MessageNoop, /* 0xc3 */
    Akao_MessageNoop, /* 0xc4 */
    Akao_MessageNoop, /* 0xc5 */
    Akao_MessageNoop, /* 0xc6 */
    Akao_MessageNoop, /* 0xc7 */
    Seq_SetGlobalPitchImmediate, /* 0xc8 */
    Seq_SlideGlobalPitchToTarget, /* 0xc9 */
    Seq_SlideGlobalPitchFromStartToTarget, /* 0xca */
    Akao_MessageNoop, /* 0xcb */
    Akao_MessageNoop, /* 0xcc */
    Akao_MessageNoop, /* 0xcd */
    Akao_MessageNoop, /* 0xce */
    Akao_MessageNoop, /* 0xcf */
    Akao_SetGlobalD2D0Immediate, /* 0xd0 */
    Akao_SlideGlobalD2D0ToTarget, /* 0xd1 */
    Akao_SlideGlobalD2D0FromStartToTarget, /* 0xd2 */
    Akao_MessageNoop, /* 0xd3 */
    Akao_SetGlobalD2CCImmediate, /* 0xd4 */
    Akao_SlideGlobalD2CCToTarget, /* 0xd5 */
    Akao_SlideGlobalD2CCFromStartToTarget, /* 0xd6 */
    Akao_MessageNoop, /* 0xd7 */
    Akao_MessageNoop, /* 0xd8 */
    Akao_MessageNoop, /* 0xd9 */
    Akao_MessageNoop, /* 0xda */
    Akao_MessageNoop, /* 0xdb */
    Akao_MessageNoop, /* 0xdc */
    Akao_MessageNoop, /* 0xdd */
    Akao_MessageNoop, /* 0xde */
    Akao_MessageNoop, /* 0xdf */
    Akao_MessageNoop, /* 0xe0 */
    Akao_MessageNoop, /* 0xe1 */
    Akao_MessageNoop, /* 0xe2 */
    Akao_MessageNoop, /* 0xe3 */
    Akao_MessageNoop, /* 0xe4 */
    Akao_MessageNoop, /* 0xe5 */
    Akao_MessageNoop, /* 0xe6 */
    Akao_MessageNoop, /* 0xe7 */
    Akao_MessageNoop, /* 0xe8 */
    Akao_MessageNoop, /* 0xe9 */
    Akao_MessageNoop, /* 0xea */
    Akao_MessageNoop, /* 0xeb */
    Akao_MessageNoop, /* 0xec */
    Akao_MessageNoop, /* 0xed */
    Akao_MessageNoop, /* 0xee */
    Akao_MessageNoop, /* 0xef */
    Akao_InitPrimarySecondaryVoices, /* 0xf0 */
    Spu_StopActiveVoices, /* 0xf1 */
    Akao_MessageNoop, /* 0xf2 */
    Akao_MessageNoop, /* 0xf3 */
    Akao_MessageNoop, /* 0xf4 */
    Akao_MessageNoop, /* 0xf5 */
    Akao_MessageNoop, /* 0xf6 */
    Akao_MessageNoop, /* 0xf7 */
    Akao_MessageNoop, /* 0xf8 */
    Akao_MessageNoop, /* 0xf9 */
    Akao_MessageNoop, /* 0xfa */
    Akao_MessageNoop, /* 0xfb */
    Akao_MessageNoop, /* 0xfc */
    Akao_MessageNoop, /* 0xfd */
    Akao_MessageNoop, /* 0xfe */
    Akao_MessageNoop, /* 0xff */
};
/* The fallback sequence ends a track; zero bytes are note opcodes. */

char g_AkaoDefaultVoiceProgram[4] = "\xa0\xa0\xa0\xa0";

/* Original USA resident data at 8009B8F8. */
short g_AkaoPanVolumeTable[256] = {
    0, 6, 24, 54, 96, 150, 219, 294, 384, 486, 600, 726,
    864, 1014, 1176, 1350, 1536, 1734, 1944, 2166, 2400, 2646, 2904, 3174,
    3456, 3750, 4056, 4374, 4704, 5046, 5400, 5766, 6144, 6534, 6936, 7350,
    7776, 8214, 8664, 9126, 9600, 10086, 10584, 11094, 11616, 12150, 12696, 13254,
    13824, 14406, 15000, 15606, 16224, 16854, 17496, 18150, 18816, 19494, 20184, 20886,
    21600, 22326, 23064, 23814, 24576, 24462, 24348, 24235, 24121, 24007, 23894, 23780,
    23667, 23553, 23439, 23326, 23212, 23098, 22985, 22871, 22758, 22644, 22530, 22417,
    22303, 22190, 22076, 21962, 21849, 21735, 21621, 21508, 21394, 21281, 21167, 21053,
    20940, 20826, 20712, 20599, 20485, 20372, 20258, 20144, 20031, 19917, 19804, 19690,
    19576, 19463, 19349, 19235, 19122, 19008, 18895, 18781, 18667, 18554, 18440, 18326,
    18213, 18099, 17986, 17872, 17758, 17645, 17531, 17418, 17418, 17141, 16865, 16588,
    16312, 16035, 15759, 15482, 15206, 14929, 14653, 14376, 14100, 13823, 13547, 13270,
    12994, 12717, 12441, 12164, 11888, 11612, 11335, 11059, 10782, 10506, 10229, 9953,
    9676, 9400, 9123, 8847, 8570, 8294, 8017, 7741, 7464, 7188, 6911, 6635,
    6358, 6082, 5806, 5529, 5253, 4976, 4700, 4423, 4147, 3870, 3594, 3317,
    3041, 2764, 2488, 2211, 1935, 1658, 1382, 1105, 829, 552, 276, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0,
};

/* Original USA resident data at 8009B85C. */
AkaoU8 g_AkaoOpcodeArgLengths[96] = {
    0, 2, 2, 2, 3, 2, 1, 1, 2, 3, 2, 3,
    2, 2, 2, 2, 3, 2, 2, 1, 4, 2, 1, 2,
    4, 2, 1, 2, 3, 2, 1, 2, 2, 2, 1, 1,
    1, 1, 1, 1, 1, 0, 0, 0, 1, 0, 2, 2,
    1, 0, 2, 2, 1, 1, 1, 1, 2, 2, 2, 0,
    2, 3, 3, 3, 1, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

/* Original USA resident data at 8009B8BC. */
AkaoU8 g_AkaoExtOpcodeArgLengths[32] = {
    3, 4, 3, 4, 3, 1, 0, 0, 0, 0, 2, 5,
    3, 1, 2, 3, 2, 1, 0, 0, 3, 3, 3, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
};

/* Original USA resident data at 8009B8DC. */
u16 g_AkaoNoteLengthTable[11] = {
    192, 96, 48, 24, 12, 6, 3, 32, 16, 8, 4,
};

/* Original USA resident data at 8009BAF8. */
static s16 g_AkaoLfoWave0[12] = {
    8191, -8191, 16383, -16383, 24575, -24575, 32767, -32767, 0, 0, -2, 0,
};

/* Original USA resident data at 8009BB10. */
static s16 g_AkaoLfoWave1[6] = {
    32767, -32767, 0, 0, -2, 0,
};

/* Original USA resident data at 8009BB1C. */
static s16 g_AkaoLfoWave2[10] = {
    8191, 0, 16383, 0, 24575, 0, 32767, 0, 0, -2,
};

/* Original USA resident data at 8009BB30. */
static s16 g_AkaoLfoWave3[6] = {
    32767, 0, 32767, 0, 0, -2,
};

/* Original USA resident data at 8009BB3C. */
static s16 g_AkaoLfoWave4[10] = {
    -8191, 0, -16383, 0, -24575, 0, -32767, 0, 0, -2,
};

/* Original USA resident data at 8009BB50. */
static s16 g_AkaoLfoWave5[6] = {
    -32767, 0, -32767, 0, 0, -2,
};

/* Original USA resident data at 8009BB5C. */
static s16 g_AkaoLfoWave6[160] = {
    0, 1280, 2530, 4813, 5790, 6625, 7297, 7789, 8090, 8191, 8092, 7793,
    7302, 6632, 5799, 4823, 3728, 2542, 1293, 0, -1268, -2518, -3706, -4803,
    -5782, -6618, -7292, -7786, -8089, -8192, -8095, -7798, -7309, -6641, -5809, -4835,
    -3741, -2556, -1307, 0, 2561, 5060, 9626, 11580, 13250, 14594, 15578, 16180,
    16383, 16184, 15586, 14605, 13265, 11599, 9647, 7457, 5085, 2587, 0, -2536,
    -5036, -7412, -9605, -11563, -13235, -14583, -15571, -16177, -16384, -16189, -15595, -14618,
    -13281, -11618, -9669, -7482, -5111, -2614, 0, 3842, 7590, 14439, 17370, 19875,
    21891, 23367, 24270, 24575, 24276, 23379, 21908, 19898, 17398, 14470, 11186, 7627,
    3881, 0, -3804, -7554, -11118, -14408, -17344, -19853, -21874, -23357, -24265, -24576,
    -24284, -23393, -21927, -19922, -17427, -14503, -11223, -7666, -3921, 0, 5123, 10120,
    19252, 23161, 26500, 29188, 31157, 32360, 32767, 32369, 31173, 29211, 26531, 23198,
    19294, 14915, 10170, 5175, 0, -5072, -10072, -14823, -19210, -23125, -26470, -29165,
    -31142, -32353, -32768, -32378, -31190, -29236, -26562, -23235, -19337, -14963, -10221, -5227,
    0, 0, -39, 0,
};

/* Original USA resident data at 8009BC9C. */
static s16 g_AkaoLfoWave7[42] = {
    0, 5123, 10120, 19252, 23161, 26500, 29188, 31157, 32360, 32767, 32369, 31173,
    29211, 26531, 23198, 19294, 14915, 10170, 5175, 0, -5072, -10072, -14823, -19210,
    -23125, -26470, -29165, -31142, -32353, -32768, -32378, -31190, -29236, -26562, -23235, -19337,
    -14963, -10221, -5227, 0, 0, -39,
};

/* Original USA resident data at 8009BCF0. */
static s16 g_AkaoLfoWave10[84] = {
    0, 2530, 4813, 6625, 7789, 8191, 7793, 6632, 4823, 2542, 13, -2518,
    -4803, -6618, -7786, -8192, -7798, -6641, -4835, -2556, 0, 5060, 9626, 13250,
    15578, 16383, 15586, 13265, 9647, 5085, 26, -5036, -9605, -13235, -15571, -16384,
    -15595, -13281, -9669, -5111, 0, 7590, 14439, 19875, 23367, 24575, 23379, 19898,
    14470, 7627, 39, -7554, -14408, -19853, -23357, -24576, -23393, -19922, -14503, -7666,
    0, 10120, 19252, 26500, 31157, 32767, 31173, 26531, 19294, 10170, 52, -10072,
    -19210, -26470, -31142, -32768, -31190, -26562, -19337, -10221, 0, 0, -20, 0,
};

/* Original USA resident data at 8009BD98. */
static s16 g_AkaoLfoWave11[24] = {
    0, 10120, 19252, 26500, 31157, 32767, 31173, 26531, 19294, 10170, 52, -10072,
    -19210, -26470, -31142, -32768, -31190, -26562, -19337, -10221, 0, 0, -20, 0,
};

/* Original USA resident data at 8009BDC8. */
static s16 g_AkaoLfoWave8[68] = {
    0, 2047, 4095, 6143, 8191, 6143, 4095, 2047, 0, -2047, -4095, -6143,
    -8191, -6143, -4095, -2047, 0, 4095, 8191, 12287, 16383, 12287, 8191, 4095,
    0, -4095, -8191, -12287, -16383, -12287, -8191, -4095, 0, 6143, 12287, 18431,
    24575, 18431, 12287, 6143, 0, -6143, -12287, -18431, -24575, -18431, -12287, -6143,
    0, 8191, 16383, 24575, 32767, 24575, 16383, 8191, 0, -8191, -16383, -24575,
    -32767, -24575, -16383, -8191, 0, 0, -16, 0,
};

/* Original USA resident data at 8009BE50. */
static s16 g_AkaoLfoWave9[20] = {
    0, 8191, 16383, 24575, 32767, 24575, 16383, 8191, 0, -8191, -16383, -24575,
    -32767, -24575, -16383, -8191, 0, 0, -16, 0,
};

/* Original USA resident data at 8009BE78. */
static s16 g_AkaoLfoWave12[260] = {
    0, 12797, 32005, -2563, -4882, 31036, 30160, 2044, -3055, -15627, 26127, 13070,
    16362, -16671, -29678, 6177, -20485, 18944, -11289, -2827, -7881, -6189, 21005, 4355,
    -20769, 19192, 1862, 28916, -28991, -26855, 13586, -2560, 6912, -19223, 15134, -20971,
    -25400, 26105, -15581, 29176, 11505, -23036, -27908, -14066, -20471, 4325, 2052, -32767,
    4858, 4376, -6391, 18424, -27175, -8932, 6921, 3060, 31740, 8447, -27644, 32252,
    -29963, -18665, 29945, 11004, -8954, -10253, -2819, 1544, -3560, 27103, -17401, 19191,
    -30229, -17625, 4361, 32751, 13845, -4915, 30241, -26603, 22223, -11258, 13090, -8218,
    -20726, -17931, -12792, 23813, -16402, -4070, 8424, -24810, -29454, -15641, -11730, -14847,
    -17968, -32500, 17187, 10461, -26889, 23612, -24610, -20760, -18917, 16115, 5128, -9495,
    11275, -20173, -22303, 1498, 2324, 20002, -24, -30735, 8688, -29403, 32252, 7408,
    24081, 15848, -14582, -28905, -16, 27149, -29, 4851, 1055, 30732, -15121, 23298,
    15618, 754, 5401, -9515, -17143, -28135, -2813, 25309, 14342, 3878, -22277, -7938,
    -2333, 3586, 528, 17414, -27436, -12022, -22458, 4337, 6860, -16899, 6704, -24341,
    24554, -16624, -17425, -8421, -15860, 10976, 16114, -20953, 20481, -1571, 13841, -21222,
    2015, 27909, 6669, -18725, 27947, 13831, -1076, 16904, 3873, 14070, -14330, 1539,
    -553, 30743, 1541, -20725, -27140, 4332, -22770, -21005, 26127, -16910, -8456, -7135,
    -19993, -787, 23324, -24311, -13070, -2573, -25861, -19433, -27928, 32520, 19450, -30211,
    15388, -785, -3365, 18973, 26397, -10497, 26074, -3588, 28170, 2572, 4600, 18946,
    23548, -5370, -23059, 12518, -32751, -11724, 2296, 17372, -2551, -22798, -26099, 28678,
    -2582, -25348, -32482, -21018, -6642, -27145, -19708, -25345, -4118, -2791, -1536, 15094,
    -14379, 10018, -17608, 17361, 30701, 18696, -12798, -21745, 15851, -9462, 25601, 16377,
    30450, -27895, -11761, 29925, 0, 0, -256, 0,
};

/* Sixteen selectors; repeated pointers deliberately share the same waveform. */
void *g_AkaoSoundEntryTable[16] = {
    g_AkaoLfoWave0, g_AkaoLfoWave1, g_AkaoLfoWave2, g_AkaoLfoWave3,
    g_AkaoLfoWave4, g_AkaoLfoWave5, g_AkaoLfoWave6, g_AkaoLfoWave7,
    g_AkaoLfoWave8, g_AkaoLfoWave9, g_AkaoLfoWave10, g_AkaoLfoWave11,
    g_AkaoLfoWave12, g_AkaoLfoWave9, g_AkaoLfoWave8, g_AkaoLfoWave9
};

/* Canonical CD/XA input mix owned by the original voice driver. */
struct CdlATV g_AkaoCdVolume;
