#ifndef PE1_GTE_H
#define PE1_GTE_H

#include "pe1/gte_types.h"
#include <string.h>

/* Existing PsyZ endpoints, without importing conflicting SDK types. */
unsigned int Psyz_GteDataRead(unsigned reg);
void Psyz_GteDataWrite(unsigned reg, unsigned int value);
unsigned int Psyz_GteCtrlRead(unsigned reg);
void Psyz_GteCtrlWrite(unsigned reg, unsigned int value);
void Psyz_GteCommand(unsigned int command);
void Psyz_GteSetPgxpMatrix(const double rot[3][3], const double trans[3]);
void Psyz_PgxpSetActive(int active);
void Psyz_PgxpNextFrame(void);
void Psyz_GteClearPgxpMatrix(void);
void Psyz_GteSetPgxpEffectMode(int enabled);
int Psyz_GteGetLastPgxpVertex(float *x, float *y, float *w);
void Psyz_GteStorePgxpVertex(const void *dst, unsigned int sxy, float x, float y, float w);
void Psyz_GteProjectFloatVertex(double vx, double vy, double vz, const void *dst, unsigned int sxy);
void Psyz_GteStoreSxyTo(unsigned reg, void *dst_addr);
void Psyz_GteCopyPgxpSxy(const void *src_addr, const void *dst_addr, unsigned int sxy);
int Psyz_PgxpLookupVertex(const void *packet_sxy_addr, unsigned int sxy,
                          float *x, float *y, float *w);
int Psyz_GtePgxpNclip3(const void *s0, unsigned int xy0, const void *s1, unsigned int xy1,
                       const void *s2, unsigned int xy2, int int_area);
int Psyz_GtePgxpNclip4(const void *s0, unsigned int xy0, const void *s1, unsigned int xy1,
                       const void *s2, unsigned int xy2, const void *s3, unsigned int xy3,
                       int int_area);

/*
 * PSY-Q-style GTE operations.  Keep these macros limited to COP2 state and
 * commands; the calling function owns all normal CPU-side algorithmic work.
 */

/* Single-instruction transfers used by ApplyMatrix and ApplyMatrixSV.
 * Hazard slots are explicit at the call site, separate from the command. */
#define GTE_INSN_LWC2_0_0 "lwc2 $0,0(%0)"
#define GTE_INSN_LWC2_1_4 "lwc2 $1,4(%0)"
#define gte_lwc2_0_0(ptr) \
    do { u32 word; memcpy(&word, (ptr), 4); Psyz_GteDataWrite(0, word); } while (0)
#define gte_lwc2_1_4(ptr) \
    do { u32 word; memcpy(&word, (const u8 *)(ptr) + 4, 4); Psyz_GteDataWrite(1, word); } while (0)
#define gte_lwc2_2_0(p) do { u32 word; memcpy(&word, (const u8 *)(p) + 0, 4); Psyz_GteDataWrite(2, word); } while (0)
#define gte_lwc2_3_4(p) do { u32 word; memcpy(&word, (const u8 *)(p) + 4, 4); Psyz_GteDataWrite(3, word); } while (0)
#define gte_lwc2_4_0(p) do { u32 word; memcpy(&word, (const u8 *)(p) + 0, 4); Psyz_GteDataWrite(4, word); } while (0)
#define gte_lwc2_5_4(p) do { u32 word; memcpy(&word, (const u8 *)(p) + 4, 4); Psyz_GteDataWrite(5, word); } while (0)
#define gte_rtpt_command() Psyz_GteCommand(0x4A280030u & 0x01ffffffu)
#define gte_avsz3_command() Psyz_GteCommand(0x4B58002Du & 0x01ffffffu)
#define gte_avsz4_command() Psyz_GteCommand(0x4B68002Eu & 0x01ffffffu)

#define gte_swc2_11_16(ptr) do { u32 word = Psyz_GteDataRead(11); memcpy((u8 *)(ptr) + 16, &word, 4); } while (0)

#define gte_swc2_25_0(ptr) do { u32 word = Psyz_GteDataRead(25); memcpy((u8 *)(ptr) + 0, &word, 4); } while (0)
#define gte_swc2_26_4(ptr) do { u32 word = Psyz_GteDataRead(26); memcpy((u8 *)(ptr) + 4, &word, 4); } while (0)
#define gte_swc2_27_8(ptr) do { u32 word = Psyz_GteDataRead(27); memcpy((u8 *)(ptr) + 8, &word, 4); } while (0)

/* MVMVA: SF=1, rotation matrix, V0, no translation, LM=0. */
#define gte_mvmva_rotation_v0_sf12() Psyz_GteCommand(0x4A486012u & 0x01ffffffu)

/* Single commands; callers place their required transfer hazard slots. */
#define gte_rtps_command() Psyz_GteCommand(0x4A180001u & 0x01ffffffu)
#define gte_mvmva_rotation_v0_translation_sf12() Psyz_GteCommand(0x4A480012u & 0x01ffffffu)

/* Individual MAC transfers and rotation commands for long-vector inputs. */
#define gte_mfc2_25(value) ((value) = Psyz_GteDataRead(25))
#define gte_mfc2_26(value) ((value) = Psyz_GteDataRead(26))
#define gte_mfc2_27(value) ((value) = Psyz_GteDataRead(27))
#define gte_mvmva_rotation_ir_sf0() Psyz_GteCommand(0x4A41E012u & 0x01ffffffu)
#define gte_mvmva_rotation_ir_sf12() Psyz_GteCommand(0x4A49E012u & 0x01ffffffu)

/* Far-colour controls and three-vertex colour output for NCCT.
 * Each macro contains exactly one GTE instruction. */
#define gte_ctc2_13(v) Psyz_GteCtrlWrite(13, (u32)(v))
#define gte_ctc2_14(v) Psyz_GteCtrlWrite(14, (u32)(v))
#define gte_ctc2_15(v) Psyz_GteCtrlWrite(15, (u32)(v))
#define gte_lwc2_6_0(p) do { u32 word; memcpy(&word, (const u8 *)(p) + 0, 4); Psyz_GteDataWrite(6, word); } while (0)
#define gte_ncct_command() Psyz_GteCommand(0x4B18043Fu & 0x01ffffffu)
#define gte_swc2_20_0(p) do { u32 word = Psyz_GteDataRead(20); memcpy((u8 *)(p) + 0, &word, 4); } while (0)
#define gte_swc2_21_0(p) do { u32 word = Psyz_GteDataRead(21); memcpy((u8 *)(p) + 0, &word, 4); } while (0)
#define gte_swc2_22_0(p) do { u32 word = Psyz_GteDataRead(22); memcpy((u8 *)(p) + 0, &word, 4); } while (0)

/* Read the current packed rotation/translation control words. */
#define gte_cfc2_0(v) ((v) = Psyz_GteCtrlRead(0))
#define gte_cfc2_1(v) ((v) = Psyz_GteCtrlRead(1))
#define gte_cfc2_2(v) ((v) = Psyz_GteCtrlRead(2))
#define gte_cfc2_3(v) ((v) = Psyz_GteCtrlRead(3))
#define gte_cfc2_4(v) ((v) = Psyz_GteCtrlRead(4))
#define gte_cfc2_5(v) ((v) = Psyz_GteCtrlRead(5))
#define gte_cfc2_6(v) ((v) = Psyz_GteCtrlRead(6))
#define gte_cfc2_7(v) ((v) = Psyz_GteCtrlRead(7))

#define gte_cop2_hazard_slot() ((void)0)

/* Perspective center in signed 16.16 screen coordinates. */
#define gte_ctc2_24(value) Psyz_GteCtrlWrite(24, (u32)(value))
#define gte_ctc2_25(value) Psyz_GteCtrlWrite(25, (u32)(value))

/* Projection distance (H). */
#define gte_ctc2_26(value) Psyz_GteCtrlWrite(26, (u32)(value))

#define gte_ctc2_0(value) Psyz_GteCtrlWrite(0, (u32)(value))
#define gte_ctc2_1(value) Psyz_GteCtrlWrite(1, (u32)(value))
#define gte_ctc2_2(value) Psyz_GteCtrlWrite(2, (u32)(value))
#define gte_ctc2_3(value) Psyz_GteCtrlWrite(3, (u32)(value))
#define gte_ctc2_4(value) Psyz_GteCtrlWrite(4, (u32)(value))
#define gte_ctc2_5(value) Psyz_GteCtrlWrite(5, (u32)(value))
#define gte_ctc2_6(value) Psyz_GteCtrlWrite(6, (u32)(value))
#define gte_ctc2_7(value) Psyz_GteCtrlWrite(7, (u32)(value))

/* Packed light-matrix words L11..L33 (control registers 8..12). */
#define gte_ctc2_8(value) Psyz_GteCtrlWrite(8, (u32)(value))
#define gte_ctc2_9(value) Psyz_GteCtrlWrite(9, (u32)(value))
#define gte_ctc2_10(value) Psyz_GteCtrlWrite(10, (u32)(value))
#define gte_ctc2_11(value) Psyz_GteCtrlWrite(11, (u32)(value))
#define gte_ctc2_12(value) Psyz_GteCtrlWrite(12, (u32)(value))

/* Packed light-color matrix words (control registers 16..20). */
#define gte_ctc2_16(value) Psyz_GteCtrlWrite(16, (u32)(value))
#define gte_ctc2_17(value) Psyz_GteCtrlWrite(17, (u32)(value))
#define gte_ctc2_18(value) Psyz_GteCtrlWrite(18, (u32)(value))
#define gte_ctc2_19(value) Psyz_GteCtrlWrite(19, (u32)(value))
#define gte_ctc2_20(value) Psyz_GteCtrlWrite(20, (u32)(value))

#define gte_mtc2_1(value) Psyz_GteDataWrite(1, (u32)(value))

#define gte_mtc2_0(value) Psyz_GteDataWrite(0, (u32)(value))
#define gte_mtc2_9(value) Psyz_GteDataWrite(9, (u32)(value))
#define gte_mtc2_10(value) Psyz_GteDataWrite(10, (u32)(value))
#define gte_mtc2_11(value) Psyz_GteDataWrite(11, (u32)(value))

#define gte_mfc2_9(out) ((out) = Psyz_GteDataRead(9))
#define gte_mfc2_10(out) ((out) = Psyz_GteDataRead(10))
#define gte_mfc2_11(out) ((out) = Psyz_GteDataRead(11))

#define gte_lwc2_9_0(ptr) do { u32 word; memcpy(&word, (const u8 *)(ptr) + 0, 4); Psyz_GteDataWrite(9, word); } while (0)
#define gte_lwc2_10_4(ptr) do { u32 word; memcpy(&word, (const u8 *)(ptr) + 4, 4); Psyz_GteDataWrite(10, word); } while (0)
#define gte_lwc2_11_8(ptr) do { u32 word; memcpy(&word, (const u8 *)(ptr) + 8, 4); Psyz_GteDataWrite(11, word); } while (0)
/* IR vector loads with precise read operands, preserving unrelated locals. */
#define gte_ldir1_precise(ptr) do { u32 word; memcpy(&word, (const u8 *)(ptr) + 0, 4); Psyz_GteDataWrite(9, word); } while (0)
#define gte_ldir2_precise(ptr) do { u32 word; memcpy(&word, (const u8 *)(ptr) + 4, 4); Psyz_GteDataWrite(10, word); } while (0)
#define gte_ldir3_precise(ptr) do { u32 word; memcpy(&word, (const u8 *)(ptr) + 8, 4); Psyz_GteDataWrite(11, word); } while (0)

#define gte_op_sf12_command() Psyz_GteCommand(0x4B78000Cu & 0x01ffffffu)

#define gte_lwc2_1_8(ptr) do { u32 word; memcpy(&word, (const u8 *)(ptr) + 8, 4); Psyz_GteDataWrite(1, word); } while (0)

/* Load V0 from an s32[3] translation (a matrix's t): the low halfwords of
 * t[0] and t[1] packed into VXY0 (narrowed to lhu), t[2] by lwc2 into VZ0.
 * source is an s32 * lvalue so callers can reuse the constrained address. */
#define gte_ldv0_word3(source) do { const u8 *pe_gte_src = (const u8 *)(source); u16 pe_gte_x, pe_gte_y; u32 pe_gte_z; memcpy(&pe_gte_x, pe_gte_src, 2); memcpy(&pe_gte_y, pe_gte_src + 4, 2); memcpy(&pe_gte_z, pe_gte_src + 8, 4); Psyz_GteDataWrite(0, pe_gte_x | ((u32)pe_gte_y << 16)); Psyz_GteDataWrite(1, pe_gte_z); } while (0)

#define gte_ldv0_word3_at(vec) \
    { \
        s32 *source = (vec); \
        gte_ldv0_word3(source); \
    }

#define gte_swc2_9_0(ptr) do { u32 word = Psyz_GteDataRead(9); memcpy((u8 *)(ptr) + 0, &word, 4); } while (0)
#define gte_swc2_10_4(ptr) do { u32 word = Psyz_GteDataRead(10); memcpy((u8 *)(ptr) + 4, &word, 4); } while (0)
#define gte_swc2_11_8(ptr) do { u32 word = Psyz_GteDataRead(11); memcpy((u8 *)(ptr) + 8, &word, 4); } while (0)

#define gte_mvmva_light_ir_sf0() Psyz_GteCommand(0x4A49E012u & 0x01ffffffu)

/* Load a strided matrix column into IR1..3 and apply RTIR12. */
#define gte_ldrtir12_matrix_column(column) do { gte_ldclmv(column); gte_rtir(); } while (0)

/* Store IR1..3 back to a strided matrix column. */
#define gte_stir123_column(out) do { u8 *pe_gte_dst = (u8 *)(out); unsigned pe_gte_i; for (pe_gte_i = 0; pe_gte_i < 3; ++pe_gte_i) { u16 pe_gte_half = (u16)Psyz_GteDataRead(9 + pe_gte_i); memcpy(pe_gte_dst + pe_gte_i * 6, &pe_gte_half, 2); } } while (0)

/* PSY-Q gte_stsv: store IR1..3 as a short vector (mfc2 into $12..$14, then
 * three halfword stores inside the same statement, as the SDK macro does). */
#define gte_stsv(out) do { u8 *pe_gte_dst = (u8 *)(out); unsigned pe_gte_i; for (pe_gte_i = 0; pe_gte_i < 3; ++pe_gte_i) { u16 pe_gte_half = (u16)Psyz_GteDataRead(9 + pe_gte_i); memcpy(pe_gte_dst + pe_gte_i * 2, &pe_gte_half, 2); } } while (0)

#define gte_stir123_column_at(column) \
    { \
        volatile short *out = (volatile short *)(column); \
        gte_stir123_column(out); \
    }

#define gte_rtv0tr_sf0() Psyz_GteCommand(0x4A480012u & 0x01ffffffu)

/* Load the five packed words which hold R11..R33. */
#define gte_ldrotmatrix(matrix) do { const u8 *pe_gte_src = (const u8 *)(matrix); unsigned pe_gte_i; for (pe_gte_i = 0; pe_gte_i < 5; ++pe_gte_i) { u32 pe_gte_word; memcpy(&pe_gte_word, pe_gte_src + 0 + pe_gte_i * 4, 4); Psyz_GteCtrlWrite(0 + pe_gte_i, pe_gte_word); } } while (0)

/* Load the three translation-vector words into TRX, TRY, and TRZ. */
#define gte_ldtransmatrix(matrix) do { const u8 *pe_gte_src = (const u8 *)(matrix); unsigned pe_gte_i; for (pe_gte_i = 0; pe_gte_i < 3; ++pe_gte_i) { u32 pe_gte_word; memcpy(&pe_gte_word, pe_gte_src + 20 + pe_gte_i * 4, 4); Psyz_GteCtrlWrite(5 + pe_gte_i, pe_gte_word); } } while (0)

/* Preserve and replace the first column of the rotation matrix. */
#define gte_getrotcol0(x, y, z) do { (x) = Psyz_GteCtrlRead(0); (y) = Psyz_GteCtrlRead(2); (z) = Psyz_GteCtrlRead(4); } while (0)

/* OP temporarily replaces R11, R22, and R33, so preserve their COP2 values. */
#define gte_pushrotcol0() { u32 pe_gte_saved_rotation[3] = {Psyz_GteCtrlRead(0), Psyz_GteCtrlRead(2), Psyz_GteCtrlRead(4)}

#define gte_poprotcol0() Psyz_GteCtrlWrite(0, pe_gte_saved_rotation[0]); Psyz_GteCtrlWrite(2, pe_gte_saved_rotation[1]); Psyz_GteCtrlWrite(4, pe_gte_saved_rotation[2]); }

#define gte_setrotcol0(x, y, z) do { Psyz_GteCtrlWrite(0, (u32)(x)); Psyz_GteCtrlWrite(2, (u32)(y)); Psyz_GteCtrlWrite(4, (u32)(z)); } while (0)

#define gte_ldv0(vec) do { const u8 *pe_gte_src = (const u8 *)(vec); u32 pe_gte_word; memcpy(&pe_gte_word, pe_gte_src, 4); Psyz_GteDataWrite(0, pe_gte_word); memcpy(&pe_gte_word, pe_gte_src + 4, 4); Psyz_GteDataWrite(1, pe_gte_word); } while (0)

#define gte_ldv1(vec) do { const u8 *pe_gte_src = (const u8 *)(vec); u32 pe_gte_word; memcpy(&pe_gte_word, pe_gte_src, 4); Psyz_GteDataWrite(2, pe_gte_word); memcpy(&pe_gte_word, pe_gte_src + 4, 4); Psyz_GteDataWrite(3, pe_gte_word); } while (0)

#define gte_ldv2(vec) do { const u8 *pe_gte_src = (const u8 *)(vec); u32 pe_gte_word; memcpy(&pe_gte_word, pe_gte_src, 4); Psyz_GteDataWrite(4, pe_gte_word); memcpy(&pe_gte_word, pe_gte_src + 4, 4); Psyz_GteDataWrite(5, pe_gte_word); } while (0)

/* MVMVA: rotation matrix, V0, no translation, SF=1 (12-bit fraction), LM=0. */
#define gte_mvmva() do { Psyz_GteCommand(0x4A486012u & 0x01ffffffu); } while (0)

/* MVMVA: light matrix, IR vector, no translation, SF=1, LM=1. */
#define gte_lightcolor() do { Psyz_GteCommand(0x4A4DA412u & 0x01ffffffu); } while (0)

#define gte_stmac(out) do { u8 *pe_gte_dst = (u8 *)(out); unsigned pe_gte_i; for (pe_gte_i = 0; pe_gte_i < 3; ++pe_gte_i) { u32 pe_gte_word = Psyz_GteDataRead(25 + pe_gte_i); memcpy(pe_gte_dst + pe_gte_i * 4, &pe_gte_word, 4); } } while (0)

/* MAC stores followed by a pointer return leave v0 available to the caller. */
#define gte_stmac_return(out) do { u8 *pe_gte_dst = (u8 *)(out); unsigned pe_gte_i; for (pe_gte_i = 0; pe_gte_i < 3; ++pe_gte_i) { u32 pe_gte_word = Psyz_GteDataRead(25 + pe_gte_i); memcpy(pe_gte_dst + pe_gte_i * 4, &pe_gte_word, 4); } } while (0)

/* LZCS/LZCR: count leading zeroes in a 32-bit value. */
#define gte_ldlzcs(value) Psyz_GteDataWrite(30, (u32)(value))

#define gte_stlzcr(out) do { u32 word = Psyz_GteDataRead(31); memcpy((u8 *)(out) + 0, &word, 4); } while (0)

/* The LZCR-to-GPR transfer needs two COP2 hazard slots. */
#define gte_getlzcr(out) do { (out) = Psyz_GteDataRead(31); } while (0)

/* NCLIP: signed screen-space triangle area from SXY0, SXY1, and SXY2. */
#define gte_ldsxy0(value) Psyz_GteDataWrite(12, (u32)(value))

#define gte_ldsxy1(value) Psyz_GteDataWrite(13, (u32)(value))

#define gte_ldsxy2(value) Psyz_GteDataWrite(14, (u32)(value))

/* PSY-Q gte_ldsxy3: load SXY0, SXY2 and SXY1 in the SDK's order. */
#define gte_ldsxy3(r0, r1, r2) do { Psyz_GteDataWrite(12, (u32)(r0)); Psyz_GteDataWrite(14, (u32)(r2)); Psyz_GteDataWrite(13, (u32)(r1)); } while (0)

#define gte_nclip() do { Psyz_GteCommand(0x4B400006u & 0x01ffffffu); } while (0)

#define gte_nclip_now() Psyz_GteCommand(0x4B400006u & 0x01ffffffu)

#define gte_getmac0(out) ((out) = Psyz_GteDataRead(24))

#define gte_stmac0(out) do { u32 word = Psyz_GteDataRead(24); memcpy((u8 *)(out) + 0, &word, 4); } while (0)

/* PSY-Q gte_stotz: store the averaged OTZ. */
#define gte_stotz(r0) do { u32 word = Psyz_GteDataRead(7); memcpy((u8 *)(r0) + 0, &word, 4); } while (0)

/* RTV0TR: rotate V0 and add the translation vector. */
#define gte_rtv0tr() do { Psyz_GteCommand(0x4A480012u & 0x01ffffffu); } while (0)

#define gte_getir1(out) ((out) = Psyz_GteDataRead(9))

#define gte_getir2(out) ((out) = Psyz_GteDataRead(10))

#define gte_getir3(out) ((out) = Psyz_GteDataRead(11))

#define gte_rtv0tr_mac() do { Psyz_GteCommand(0x4A480012u & 0x01ffffffu); } while (0)

#define gte_stir123(out) do { { u32 pe_gte_word = Psyz_GteDataRead(9); memcpy((u8 *)(out) + 0, &pe_gte_word, 4); } { u32 pe_gte_word = Psyz_GteDataRead(10); memcpy((u8 *)(out) + 4, &pe_gte_word, 4); } { u32 pe_gte_word = Psyz_GteDataRead(11); memcpy((u8 *)(out) + 8, &pe_gte_word, 4); } } while (0)

/* The GPL wrappers consume v1 before transferring the final IR vector. */
#define gte_stir123_gpl(out) do { { u32 pe_gte_word = Psyz_GteDataRead(9); memcpy((u8 *)(out) + 0, &pe_gte_word, 4); } { u32 pe_gte_word = Psyz_GteDataRead(10); memcpy((u8 *)(out) + 4, &pe_gte_word, 4); } { u32 pe_gte_word = Psyz_GteDataRead(11); memcpy((u8 *)(out) + 8, &pe_gte_word, 4); } } while (0)

/* RTIR12: rotate the current IR vector with no translation. */
#define gte_ldir123(x, y, z) do { Psyz_GteDataWrite(9, (u32)(x)); Psyz_GteDataWrite(10, (u32)(y)); Psyz_GteDataWrite(11, (u32)(z)); } while (0)

/* Load the three scalar IR inputs from consecutive words. */
#define gte_ldir123v(vec) do { { u32 pe_gte_word; memcpy(&pe_gte_word, (const u8 *)(vec) + 0, 4); Psyz_GteDataWrite(9, pe_gte_word); } { u32 pe_gte_word; memcpy(&pe_gte_word, (const u8 *)(vec) + 4, 4); Psyz_GteDataWrite(10, pe_gte_word); } { u32 pe_gte_word; memcpy(&pe_gte_word, (const u8 *)(vec) + 8, 4); Psyz_GteDataWrite(11, pe_gte_word); } } while (0)

/* OP consumes IR3 before IR1 and IR2 in the original PSY-Q wrapper. */
#define gte_ldopv(vec) do { { u32 pe_gte_word; memcpy(&pe_gte_word, (const u8 *)(vec) + 8, 4); Psyz_GteDataWrite(11, pe_gte_word); } { u32 pe_gte_word; memcpy(&pe_gte_word, (const u8 *)(vec) + 0, 4); Psyz_GteDataWrite(9, pe_gte_word); } { u32 pe_gte_word; memcpy(&pe_gte_word, (const u8 *)(vec) + 4, 4); Psyz_GteDataWrite(10, pe_gte_word); } } while (0)

/* OP uses R11, R22, and R33 as its temporary diagonal matrix entries. */
#define gte_ldopv1(vec) do { const u8 *pe_gte_src = (const u8 *)(vec); unsigned pe_gte_i; for (pe_gte_i = 0; pe_gte_i < 3; ++pe_gte_i) { u32 pe_gte_word; memcpy(&pe_gte_word, pe_gte_src + pe_gte_i * 4, 4); Psyz_GteCtrlWrite(pe_gte_i * 2, pe_gte_word); } } while (0)

/* RGB is the colour input used by the colour interpolation commands. */
#define gte_ldrgb(rgb) do { u32 word; memcpy(&word, (const u8 *)(rgb) + 0, 4); Psyz_GteDataWrite(6, word); } while (0)

#define gte_strgb(out) do { u32 word = Psyz_GteDataRead(22); memcpy((u8 *)(out) + 0, &word, 4); } while (0)

#define gte_ldrgb0(rgb) do { u32 word; memcpy(&word, (const u8 *)(rgb) + 0, 4); Psyz_GteDataWrite(20, word); } while (0)

#define gte_ldrgb1(rgb) do { u32 word; memcpy(&word, (const u8 *)(rgb) + 0, 4); Psyz_GteDataWrite(21, word); } while (0)

#define gte_ldrgb2(rgb) do { u32 word; memcpy(&word, (const u8 *)(rgb) + 0, 4); Psyz_GteDataWrite(22, word); } while (0)

#define gte_strgb0(out) do { u32 word = Psyz_GteDataRead(20); memcpy((u8 *)(out) + 0, &word, 4); } while (0)

#define gte_strgb1(out) do { u32 word = Psyz_GteDataRead(21); memcpy((u8 *)(out) + 0, &word, 4); } while (0)

#define gte_strgb2(out) do { u32 word = Psyz_GteDataRead(22); memcpy((u8 *)(out) + 0, &word, 4); } while (0)

#define gte_declare_three_outputs()                                         \
    void *gte_out0;                                      \
    void *gte_out1;                                      \
    void *gte_out2

#define gte_bind_three_outputs(out0, out1, out2)                            \
    {                                                                     \
        gte_out0 = (out0);                                                   \
        gte_out1 = (out1);                                                   \
        gte_out2 = (out2);                                                   \
    }

#define gte_dpct_separate() do { Psyz_GteCommand(0x4AF8002Au & 0x01ffffffu); } while (0)

#define gte_rtpt_three_outputs() do { Psyz_GteCommand(0x4A280030u & 0x01ffffffu); } while (0)

#define gte_rtps_three_outputs() do { Psyz_GteCommand(0x4A180001u & 0x01ffffffu); } while (0)

#define gte_strgb0_bound() do { u32 pe_gte_word = Psyz_GteDataRead(20); memcpy(gte_out0, &pe_gte_word, 4); } while (0)
#define gte_strgb1_bound() do { u32 pe_gte_word = Psyz_GteDataRead(21); memcpy(gte_out1, &pe_gte_word, 4); } while (0)
#define gte_strgb2_bound() do { u32 pe_gte_word = Psyz_GteDataRead(22); memcpy(gte_out2, &pe_gte_word, 4); } while (0)
#define gte_stsxy_three_0() do { Psyz_GteStoreSxyTo(12, gte_out0); } while (0)
#define gte_stsxy_three_1() do { Psyz_GteStoreSxyTo(13, gte_out1); } while (0)
#define gte_stsxy_three_2() do { Psyz_GteStoreSxyTo(14, gte_out2); } while (0)
#define gte_stsxy2_three_0() do { Psyz_GteStoreSxyTo(14, gte_out0); } while (0)
#define gte_stir0_three_1() do { u32 pe_gte_word = Psyz_GteDataRead(8); memcpy(gte_out1, &pe_gte_word, 4); } while (0)

#define gte_getflag(out) ((out) = Psyz_GteCtrlRead(31))

#define gte_declare_rt4_results(first_flags, flags, depth)                   \
    int first_flags;                                     \
    int flags;                                           \
    int depth

/* IR0 is the scalar input used by GPF. */
#define gte_ldir0(value) Psyz_GteDataWrite(8, (u32)(value))

#define gte_ldmac123(x, y, z) do { Psyz_GteDataWrite(25, (u32)(x)); Psyz_GteDataWrite(26, (u32)(y)); Psyz_GteDataWrite(27, (u32)(z)); } while (0)

#define gte_lddqa(value) Psyz_GteCtrlWrite(27, (u32)(value))

#define gte_lddqb(value) Psyz_GteCtrlWrite(28, (u32)(value))

/* SQR squares IR1..IR3 into MAC1..MAC3. */
#define gte_sqr() do { Psyz_GteCommand(0x4AA00428u & 0x01ffffffu); } while (0)

/* SQR with the PSY-Q SF flag set. */
#define gte_sqr12() do { Psyz_GteCommand(0x4AA80428u & 0x01ffffffu); } while (0)

/* AVSZ uses the four depth FIFO registers and writes OTZ. */
#define gte_ldsz3(z1, z2, z3) do { Psyz_GteDataWrite(17, (u32)(z1)); Psyz_GteDataWrite(18, (u32)(z2)); Psyz_GteDataWrite(19, (u32)(z3)); } while (0)

#define gte_ldsz4(z0, z1, z2, z3) do { Psyz_GteDataWrite(16, (u32)(z0)); Psyz_GteDataWrite(17, (u32)(z1)); Psyz_GteDataWrite(18, (u32)(z2)); Psyz_GteDataWrite(19, (u32)(z3)); } while (0)

#define gte_avsz3() do { Psyz_GteCommand(0x4B58002Du & 0x01ffffffu); } while (0)

#define gte_avsz4() do { Psyz_GteCommand(0x4B68002Eu & 0x01ffffffu); } while (0)

#define gte_getotz(out) ((out) = Psyz_GteDataRead(7))

#define gte_getflag(out) ((out) = Psyz_GteCtrlRead(31))

/* Colour interpolation commands with the current IR vector and IR0. */
#define gte_dpcl() do { Psyz_GteCommand(0x4A680029u & 0x01ffffffu); } while (0)

#define gte_intpl() do { Psyz_GteCommand(0x4A980011u & 0x01ffffffu); } while (0)

/* GPF multiplies IR1..IR3 by IR0 into MAC1..MAC3. */
#define gte_gpf() do { Psyz_GteCommand(0x4B90003Du & 0x01ffffffu); } while (0)

#define gte_gpf0() do { Psyz_GteCommand(0x4B90003Du & 0x01ffffffu); } while (0)

#define gte_gpf12() do { Psyz_GteCommand(0x4B98003Du & 0x01ffffffu); } while (0)

#define gte_gpl0() do { Psyz_GteCommand(0x4BA0003Eu & 0x01ffffffu); } while (0)

#define gte_gpl12() do { Psyz_GteCommand(0x4BA8003Eu & 0x01ffffffu); } while (0)

/* Fixed-staging used by the handwritten PSY-Q transfer wrappers. */
#define gte_declare_xy_staging()                                             \
    int gte_x;                                            \
    int gte_y

#define gte_declare_xyz_staging()                                            \
    gte_declare_xy_staging();                                                \
    int gte_z

#define gte_declare_shift12() int gte_shift
#define gte_set_shift12() (gte_shift = 12)

#define gte_load_byte2(input)                                                \
    {                                                                     \
        gte_x = ((unsigned char *)(input))[0];                               \
        gte_y = ((unsigned char *)(input))[1];                               \
    }

#define gte_load_byte3(input)                                                \
    {                                                                     \
        gte_load_byte2(input);                                               \
        gte_z = ((unsigned char *)(input))[2];                               \
    }

#define gte_ldir0_ir12(scale) do { Psyz_GteDataWrite(8, (u32)(scale)); Psyz_GteDataWrite(9, (u32)gte_x); Psyz_GteDataWrite(10, (u32)gte_y); } while (0)

#define gte_ldir0_ir123(scale) do { Psyz_GteDataWrite(8, (u32)(scale)); Psyz_GteDataWrite(9, (u32)gte_x); Psyz_GteDataWrite(10, (u32)gte_y); Psyz_GteDataWrite(11, (u32)gte_z); } while (0)

/* GPL without an inserted hazard slot, for wrappers which already provide it. */
#define gte_gpl0_now() Psyz_GteCommand(0x4BA0003Eu & 0x01ffffffu)
#define gte_gpl12_now() Psyz_GteCommand(0x4BA8003Eu & 0x01ffffffu)

/* The extra inputs keep the caller's output/shift setup before MAC reads. */
#define gte_getmac12_staged(out) do { gte_x = Psyz_GteDataRead(25); gte_y = Psyz_GteDataRead(26); } while (0)

#define gte_getmac123_staged(out) do { gte_x = Psyz_GteDataRead(25); gte_y = Psyz_GteDataRead(26); gte_z = Psyz_GteDataRead(27); } while (0)

#define gte_op0() do { Psyz_GteCommand(0x4B70000Cu & 0x01ffffffu); } while (0)

#define gte_op12() do { Psyz_GteCommand(0x4B78000Cu & 0x01ffffffu); } while (0)

#define gte_getlzcr_now(out) ((out) = Psyz_GteDataRead(31))

#define gte_getmac123(x, y, z) do { (x) = Psyz_GteDataRead(25); (y) = Psyz_GteDataRead(26); (z) = Psyz_GteDataRead(27); } while (0)

#define gte_rtir12() do { Psyz_GteCommand(0x4A49E012u & 0x01ffffffu); } while (0)

/* Individual triple-vertex transfers for the morph projection path. */
#define gte_lwc2_2_8(p) do { u32 word; memcpy(&word, (const u8 *)(p) + 8, 4); Psyz_GteDataWrite(2, word); } while (0)
#define gte_lwc2_3_12(p) do { u32 word; memcpy(&word, (const u8 *)(p) + 12, 4); Psyz_GteDataWrite(3, word); } while (0)
#define gte_lwc2_4_16(p) do { u32 word; memcpy(&word, (const u8 *)(p) + 16, 4); Psyz_GteDataWrite(4, word); } while (0)
#define gte_lwc2_5_20(p) do { u32 word; memcpy(&word, (const u8 *)(p) + 20, 4); Psyz_GteDataWrite(5, word); } while (0)
#define gte_swc2_12_0(p) do { Psyz_GteStoreSxyTo(12, (u8 *)(p) + 0); } while (0)
#define gte_swc2_13_4(p) do { Psyz_GteStoreSxyTo(13, (u8 *)(p) + 4); } while (0)
#define gte_swc2_14_8(p) do { Psyz_GteStoreSxyTo(14, (u8 *)(p) + 8); } while (0)
#define gte_swc2_17_0(p) do { u32 word = Psyz_GteDataRead(17); memcpy((u8 *)(p) + 0, &word, 4); } while (0)
#define gte_swc2_18_4(p) do { u32 word = Psyz_GteDataRead(18); memcpy((u8 *)(p) + 4, &word, 4); } while (0)
#define gte_swc2_19_8(p) do { u32 word = Psyz_GteDataRead(19); memcpy((u8 *)(p) + 8, &word, 4); } while (0)

/* RTPS: project V0 and update SXY2/SZ3. */
#define gte_rtps() do { Psyz_GteCommand(0x4A180001u & 0x01ffffffu); } while (0)

#define gte_stsxy2(out) do { Psyz_GteStoreSxyTo(14, (u8 *)(out) + 0); } while (0)

#define gte_getsz3(out) ((out) = Psyz_GteDataRead(19))

#define gte_getsz3_for_store(value, out) do { (value) = Psyz_GteDataRead(19); } while (0)

/* RTPT: project V0, V1, and V2. */
#define gte_ldv012(vec) do { { u32 pe_gte_word; memcpy(&pe_gte_word, (const u8 *)(vec) + 0, 4); Psyz_GteDataWrite(0, pe_gte_word); } { u32 pe_gte_word; memcpy(&pe_gte_word, (const u8 *)(vec) + 4, 4); Psyz_GteDataWrite(1, pe_gte_word); } { u32 pe_gte_word; memcpy(&pe_gte_word, (const u8 *)(vec) + 8, 4); Psyz_GteDataWrite(2, pe_gte_word); } { u32 pe_gte_word; memcpy(&pe_gte_word, (const u8 *)(vec) + 12, 4); Psyz_GteDataWrite(3, pe_gte_word); } { u32 pe_gte_word; memcpy(&pe_gte_word, (const u8 *)(vec) + 16, 4); Psyz_GteDataWrite(4, pe_gte_word); } { u32 pe_gte_word; memcpy(&pe_gte_word, (const u8 *)(vec) + 20, 4); Psyz_GteDataWrite(5, pe_gte_word); } } while (0)

#define gte_rtpt() do { Psyz_GteCommand(0x4A280030u & 0x01ffffffu); } while (0)

#define gte_declare_separate_outputs()                                      \
    void *gte_out1;                                      \
    void *gte_out2;                                      \
    void *gte_depth_out;                                \
    void *gte_flags_out;                                \
    int gte_flags;                                       \
    int gte_depth_value

/* Keep the ABI-bound output registers free until RTPT has completed. */
#define gte_rtpt_separate() do { Psyz_GteCommand(0x4A280030u & 0x01ffffffu); } while (0)

#define gte_bind_separate_outputs(out1, out2, depth, flags)                  \
    {                                                                     \
        gte_out1 = (out1);                                                   \
        gte_out2 = (out2);                                                   \
        gte_depth_out = (depth);                                             \
        gte_flags_out = (flags);                                             \
    }

#define gte_stsxy0(out) do { Psyz_GteStoreSxyTo(12, (u8 *)(out) + 0); } while (0)
#define gte_stsxy1(out) do { Psyz_GteStoreSxyTo(13, (u8 *)(out) + 0); } while (0)
#define gte_stsxy1_bound() do { Psyz_GteStoreSxyTo(13, gte_out1); } while (0)
#define gte_stsxy2_bound() do { Psyz_GteStoreSxyTo(14, gte_out2); } while (0)
#define gte_stir0(out) do { u32 word = Psyz_GteDataRead(8); memcpy((u8 *)(out) + 0, &word, 4); } while (0)
#define gte_stir0_bound() do { u32 pe_gte_word = Psyz_GteDataRead(8); memcpy(gte_depth_out, &pe_gte_word, 4); } while (0)
#define gte_getflag_bound() do { gte_flags = Psyz_GteCtrlRead(31); } while (0)
#define gte_getsz3_bound() do { gte_depth_value = Psyz_GteDataRead(19); } while (0)
#define gte_stsxy012(out) do { Psyz_GteStoreSxyTo(12, (u8 *)(out) + 0); Psyz_GteStoreSxyTo(13, (u8 *)(out) + 4); Psyz_GteStoreSxyTo(14, (u8 *)(out) + 8); } while (0)

#define gte_stsz123(out) do { { u32 pe_gte_word = Psyz_GteDataRead(17); memcpy((u8 *)(out) + 0, &pe_gte_word, 4); } { u32 pe_gte_word = Psyz_GteDataRead(18); memcpy((u8 *)(out) + 4, &pe_gte_word, 4); } { u32 pe_gte_word = Psyz_GteDataRead(19); memcpy((u8 *)(out) + 8, &pe_gte_word, 4); } } while (0)

/* Single COP2 transfers used by the subdividing polygon renderer.
 * Precise memory operands describe the touched word instead of all memory.
 * The keep variants retain allocator references (one VXY0 input, one extra
 * SXY1 address input, three extra SXY2 address inputs); these are matching
 * debt, not additional hardware operations. Padded commands retain the two
 * retail transfer-hazard slots. */
#define gte_ldvxy0_precise_keep(address, keep) do { u32 word; memcpy(&word, (const u8 *)(address) + 0, 4); Psyz_GteDataWrite(0, word); } while (0)
#define gte_stsxy1_precise_keep_address(address) do { Psyz_GteStoreSxyTo(13, (u8 *)(address) + 0); } while (0)
#define gte_stsxy2_precise_keep_address(address) do { Psyz_GteStoreSxyTo(14, (u8 *)(address) + 0); } while (0)
#define gte_ldvxy0_mem(address) do { u32 pe_gte_word; memcpy(&pe_gte_word, (const u8 *)(address) + 0, 4); Psyz_GteDataWrite(0, pe_gte_word); } while (0)
#define gte_ldvz0_mem(address) do { u32 pe_gte_word; memcpy(&pe_gte_word, (const u8 *)(address) + 4, 4); Psyz_GteDataWrite(1, pe_gte_word); } while (0)
#define gte_ldvxy0_precise(address) do { u32 word; memcpy(&word, (const u8 *)(address) + 0, 4); Psyz_GteDataWrite(0, word); } while (0)
#define gte_ldvz0_precise(address) do { u32 word; memcpy(&word, (const u8 *)(address) + 4, 4); Psyz_GteDataWrite(1, word); } while (0)
#define gte_ldvxy1_precise(address) do { u32 word; memcpy(&word, (const u8 *)(address) + 0, 4); Psyz_GteDataWrite(2, word); } while (0)
#define gte_ldvz1_precise(address) do { u32 word; memcpy(&word, (const u8 *)(address) + 4, 4); Psyz_GteDataWrite(3, word); } while (0)
#define gte_ldvxy2_precise(address) do { u32 word; memcpy(&word, (const u8 *)(address) + 0, 4); Psyz_GteDataWrite(4, word); } while (0)
#define gte_ldvz2_precise(address) do { u32 word; memcpy(&word, (const u8 *)(address) + 4, 4); Psyz_GteDataWrite(5, word); } while (0)
#define gte_stsxy2_precise(address) do { Psyz_GteStoreSxyTo(14, (u8 *)(address) + 0); } while (0)
#define gte_ldvxy1_mem(address) do { u32 pe_gte_word; memcpy(&pe_gte_word, (const u8 *)(address) + 0, 4); Psyz_GteDataWrite(2, pe_gte_word); } while (0)
#define gte_stsxy2_mem(address) do { Psyz_GteStoreSxyTo(14, (address)); } while (0)
#define gte_stmac0_precise(address) do { u32 word = Psyz_GteDataRead(24); memcpy((u8 *)(address) + 0, &word, 4); } while (0)
#define gte_stotz_precise(address) do { u32 word = Psyz_GteDataRead(7); memcpy((u8 *)(address) + 0, &word, 4); } while (0)
#define gte_stsxy0_precise(address) do { Psyz_GteStoreSxyTo(12, (u8 *)(address) + 0); } while (0)
#define gte_stsxy1_precise(address) do { Psyz_GteStoreSxyTo(13, (u8 *)(address) + 0); } while (0)

/* PSY-Q style three-operand transfers: load V0..V2 from three vectors and
 * store SXY0..SXY2 to three addresses in one statement. */
#define gte_ldv3(r1, r2, r3) do { gte_ldv0(r1); gte_ldv1(r2); gte_ldv2(r3); } while (0)

#define gte_stsxy3(r1, r2, r3) do { Psyz_GteStoreSxyTo(12, (u8 *)(r1) + 0); Psyz_GteStoreSxyTo(13, (u8 *)(r2) + 0); Psyz_GteStoreSxyTo(14, (u8 *)(r3) + 0); } while (0)

/* Store SZ3 / 4 through a pointer: the ordering-table depth of a projected
 * primitive (PSY-Q gte_stszotz). */
#define gte_stszotz(out) do { u32 pe_gte_word = Psyz_GteDataRead(19) >> 2; memcpy((out), &pe_gte_word, 4); } while (0)

#define gte_rtpt_padded() Psyz_GteCommand(0x4A280030u & 0x01ffffffu)
#define gte_avsz3_padded() Psyz_GteCommand(0x4B58002Du & 0x01ffffffu)

/* PSY-Q gte_ldclmv / gte_rtir / gte_stclmv: one strided matrix column through
 * RTIR (sf=1), the three steps of the SDK's inline gte_MulMatrix0. */
#define gte_ldclmv(column) do { const u8 *pe_gte_src = (const u8 *)(column); unsigned pe_gte_i; for (pe_gte_i = 0; pe_gte_i < 3; ++pe_gte_i) { u16 pe_gte_half; memcpy(&pe_gte_half, pe_gte_src + pe_gte_i * 6, 2); Psyz_GteDataWrite(9 + pe_gte_i, pe_gte_half); } } while (0)

#define gte_rtir() Psyz_GteCommand(0x4A49E012u & 0x01ffffffu)

#define gte_stclmv(column) do { u8 *pe_gte_dst = (u8 *)(column); unsigned pe_gte_i; for (pe_gte_i = 0; pe_gte_i < 3; ++pe_gte_i) { u16 pe_gte_half = (u16)Psyz_GteDataRead(9 + pe_gte_i); memcpy(pe_gte_dst + pe_gte_i * 6, &pe_gte_half, 2); } } while (0)

/* PSY-Q gte_ldlv0 / gte_rt / gte_stlvl: a long vector through RT with
 * translation (sf=1), as used by the SDK's inline gte_CompMatrix. */
#define gte_ldlv0(vector) do { const u8 *pe_gte_src = (const u8 *)(vector); u16 pe_gte_x, pe_gte_y; u32 pe_gte_z; memcpy(&pe_gte_x, pe_gte_src, 2); memcpy(&pe_gte_y, pe_gte_src + 4, 2); memcpy(&pe_gte_z, pe_gte_src + 8, 4); Psyz_GteDataWrite(0, pe_gte_x | ((u32)pe_gte_y << 16)); Psyz_GteDataWrite(1, pe_gte_z); } while (0)

#define gte_rt() Psyz_GteCommand(0x4A480012u & 0x01ffffffu)

/* PSY-Q inline_c.h gte_stlvl: store IR1..3 as a long vector. */
#define gte_stlvl(vector) do { u8 *pe_gte_dst = (u8 *)(vector); unsigned pe_gte_i; for (pe_gte_i = 0; pe_gte_i < 3; ++pe_gte_i) { u32 pe_gte_word = Psyz_GteDataRead(9 + pe_gte_i); memcpy(pe_gte_dst + pe_gte_i * 4, &pe_gte_word, 4); } } while (0)

/* PSY-Q inline_c.h gte_MulMatrix0 / gte_CompMatrix (r3 = r1 * r2, with
 * the composed translation for CompMatrix). */
#define gte_MulMatrix0(r1, r2, r3) \
    { \
        gte_ldrotmatrix(r1); \
        gte_ldclmv(r2); \
        gte_rtir(); \
        gte_stclmv(r3); \
        gte_ldclmv((char *)(r2) + 2); \
        gte_rtir(); \
        gte_stclmv((char *)(r3) + 2); \
        gte_ldclmv((char *)(r2) + 4); \
        gte_rtir(); \
        gte_stclmv((char *)(r3) + 4); \
    }

#define gte_CompMatrix(r1, r2, r3) \
    { \
        gte_MulMatrix0(r1, r2, r3); \
        gte_ldtransmatrix(r1); \
        gte_ldlv0((char *)(r2) + 20); \
        gte_rt(); \
        gte_stlvl((char *)(r3) + 20); \
    }

/* gte_CompMatrix storing the composed translation from MAC1..3
 * (gte_stlvnl) instead of IR1..3; the skinned and bone transforms use it. */
#define gte_CompMatrixMac(r1, r2, r3) \
    { \
        gte_MulMatrix0(r1, r2, r3); \
        gte_ldtransmatrix(r1); \
        gte_ldlv0((char *)(r2) + 20); \
        gte_rt(); \
        gte_stlvnl((char *)(r3) + 20); \
    }

/* Stock PSY-Q inline_c.h (DMPSX version 3) forms, copied for
 * and instruction for instruction.  The SDK writes the COP2 commands
 * as DMPSX placeholder words; the real command words DMPSX substitutes are
 * used here.  Kept separate from the older gte_ldopv1 / gte_op12 shapes above,
 * which matched code depends on. */

/* PSY-Q gte_ldopv1: R11, R22, R33 from a long vector through $12..$14. */
#define gte_ldopv1_psyq(r0) do { const u8 *pe_gte_src = (const u8 *)(r0); unsigned pe_gte_i; for (pe_gte_i = 0; pe_gte_i < 3; ++pe_gte_i) { u32 pe_gte_word; memcpy(&pe_gte_word, pe_gte_src + pe_gte_i * 4, 4); Psyz_GteCtrlWrite(pe_gte_i * 2, pe_gte_word); } } while (0)

/* PSY-Q gte_SetGeomOffset: OFX/OFY from two integers shifted to 16.16. */
#define gte_SetGeomOffset(r0, r1) do { Psyz_GteCtrlWrite(24, (u32)(r0) << 16); Psyz_GteCtrlWrite(25, (u32)(r1) << 16); } while (0)

/* PSY-Q gte_ldopv2: IR3, IR1, IR2 from a long vector. */
#define gte_ldopv2(r0) do { { u32 pe_gte_word; memcpy(&pe_gte_word, (const u8 *)(r0) + 8, 4); Psyz_GteDataWrite(11, pe_gte_word); } { u32 pe_gte_word; memcpy(&pe_gte_word, (const u8 *)(r0) + 0, 4); Psyz_GteDataWrite(9, pe_gte_word); } { u32 pe_gte_word; memcpy(&pe_gte_word, (const u8 *)(r0) + 4, 4); Psyz_GteDataWrite(10, pe_gte_word); } } while (0)

/* PSY-Q gte_op12: two hazard nops, then OP with sf=1. */
#define gte_op12_psyq() do { Psyz_GteCommand(0x4B78000Cu & 0x01ffffffu); } while (0)

/* PSY-Q gte_SetBackColor: BK from three colour components scaled by 16. */
#define gte_SetBackColor(r0, r1, r2) do { Psyz_GteCtrlWrite(13, (u32)(r0) << 4); Psyz_GteCtrlWrite(14, (u32)(r1) << 4); Psyz_GteCtrlWrite(15, (u32)(r2) << 4); } while (0)

/* PSY-Q gte_SetLightMatrix: the five packed words of L11..L33. */
#define gte_SetLightMatrix(r0) do { const u8 *pe_gte_src = (const u8 *)(r0); unsigned pe_gte_i; for (pe_gte_i = 0; pe_gte_i < 5; ++pe_gte_i) { u32 pe_gte_word; memcpy(&pe_gte_word, pe_gte_src + 0 + pe_gte_i * 4, 4); Psyz_GteCtrlWrite(8 + pe_gte_i, pe_gte_word); } } while (0)

/* PSY-Q gte_ncct: two hazard nops, then NCCT. */
#define gte_ncct() Psyz_GteCommand(0x4B18043Fu & 0x01ffffffu)

/* PSY-Q gte_strgb3: store RGB0..RGB2 to three addresses. */
#define gte_strgb3(r0, r1, r2) do { { u32 pe_gte_word = Psyz_GteDataRead(20); memcpy((u8 *)(r0) + 0, &pe_gte_word, 4); } { u32 pe_gte_word = Psyz_GteDataRead(21); memcpy((u8 *)(r1) + 0, &pe_gte_word, 4); } { u32 pe_gte_word = Psyz_GteDataRead(22); memcpy((u8 *)(r2) + 0, &pe_gte_word, 4); } } while (0)

/* PSY-Q gte_stlvnl: store MAC1..3 as a long vector. */
#define gte_stlvnl(r0) do { u8 *pe_gte_dst = (u8 *)(r0); unsigned pe_gte_i; for (pe_gte_i = 0; pe_gte_i < 3; ++pe_gte_i) { u32 pe_gte_word = Psyz_GteDataRead(25 + pe_gte_i); memcpy(pe_gte_dst + pe_gte_i * 4, &pe_gte_word, 4); } } while (0)

/* Store the whole GTE matrix: R11..R33 as five packed words (PSY-Q
 * gte_ReadRotMatrix) followed by TRX, TRY, TRZ at +20. */
#define gte_ReadMatrix(r0) do { u8 *pe_gte_dst = (u8 *)(r0); unsigned pe_gte_i; for (pe_gte_i = 0; pe_gte_i < 8; ++pe_gte_i) { u32 pe_gte_word = Psyz_GteCtrlRead(pe_gte_i); memcpy(pe_gte_dst + pe_gte_i * 4, &pe_gte_word, 4); } } while (0)

/* PSY-Q gte_ldv3c: V0..V2 from three consecutive short vectors. */
#define gte_ldv3c(r0) do { const u8 *pe_gte_vectors = (const u8 *)(r0); gte_ldv0(pe_gte_vectors); gte_ldv1(pe_gte_vectors + 8); gte_ldv2(pe_gte_vectors + 16); } while (0)

/* PSY-Q gte_stsxy3c / gte_stsz3c: SXY0..2 and SZ1..3 to consecutive words. */
#define gte_stsxy3c(r0) do { u8 *pe_gte_dst = (u8 *)(r0); unsigned pe_gte_i; for (pe_gte_i = 0; pe_gte_i < 3; ++pe_gte_i) { Psyz_GteStoreSxyTo(12 + pe_gte_i, pe_gte_dst + pe_gte_i * 4); } } while (0)
#define gte_stsz3c(r0) do { u8 *pe_gte_dst = (u8 *)(r0); unsigned pe_gte_i; for (pe_gte_i = 0; pe_gte_i < 3; ++pe_gte_i) { u32 pe_gte_word = Psyz_GteDataRead(17 + pe_gte_i); memcpy(pe_gte_dst + pe_gte_i * 4, &pe_gte_word, 4); } } while (0)

/* PSY-Q gte_SetColorMatrix: the five packed words of LR1..LB3. */
#define gte_SetColorMatrix(r0) do { const u8 *pe_gte_src = (const u8 *)(r0); unsigned pe_gte_i; for (pe_gte_i = 0; pe_gte_i < 5; ++pe_gte_i) { u32 pe_gte_word; memcpy(&pe_gte_word, pe_gte_src + 0 + pe_gte_i * 4, 4); Psyz_GteCtrlWrite(16 + pe_gte_i, pe_gte_word); } } while (0)

#endif
