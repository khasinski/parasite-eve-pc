#ifndef PE1_PORT_FEATURES_H
#define PE1_PORT_FEATURES_H

/* Optional native presentation features. Desktop builds keep them all;
 * PE_SMALL_MEMORY builds turn them off at compile time, because the
 * Dreamcast has neither the RAM nor the double-precision FPU they need. */

/* PGXP: subpixel geometry from double-precision bone and model matrices.
 * Dynamic actor lighting reads the same matrices and goes with it. */
#ifndef PE_PGXP
#define PE_PGXP 1
#endif

/* Display-only frame interpolation (60 Hz presentation). */
#ifndef PE_FRAME_INTERPOLATION
#define PE_FRAME_INTERPOLATION 1
#endif

#if PE_FRAME_INTERPOLATION
/* Calls into the backend's frame interpolation. */
#define PE_INTERP_CALL(call) call
#define PE_INTERP_ACTIVE() Psyz_GpuGetFrameInterpolation()
#else
#define PE_INTERP_CALL(call) ((void)0)
#define PE_INTERP_ACTIVE() 0
#endif

#endif /* PE1_PORT_FEATURES_H */
