#ifndef PE1_MOVIE_PROFILE_H
#define PE1_MOVIE_PROFILE_H

/* Per-stage timing of the movie players, printed every 60 movie frames.
 * Enabled with PE_MOVIE_PROFILE=1 (the Dreamcast build); otherwise the
 * macros compile to nothing. */
#include <stdint.h>

enum {
    PE_MOVIE_READ,   /* image sectors for the next frame */
    PE_MOVIE_VLC,    /* DecDCTvlc2 */
    PE_MOVIE_MDEC,   /* DecDCTin/DecDCTout, including the slice uploads */
    PE_MOVIE_SYNC,   /* waiting for the last slice */
    PE_MOVIE_INPUT,  /* pad and state handling */
    PE_MOVIE_RENDER, /* Draw_RenderFrame, including VSync */
    PE_MOVIE_STAGES
};

#if defined(PE_MOVIE_PROFILE) && PE_MOVIE_PROFILE
uint64_t Psyz_OsNowUs(void);
typedef struct {
    uint64_t us[PE_MOVIE_STAGES];
    uint64_t start_us;
    uint32_t frames;
} PeMovieProfile;
extern PeMovieProfile g_PeMovieProfile;
void PeMovieProfile_Frame(void);
#define PE_MOVIE_BEGIN(name) uint64_t pe_movie_##name = Psyz_OsNowUs()
#define PE_MOVIE_END(name, stage) \
    (g_PeMovieProfile.us[stage] += Psyz_OsNowUs() - pe_movie_##name)
#else
#define PE_MOVIE_BEGIN(name) ((void)0)
#define PE_MOVIE_END(name, stage) ((void)0)
#define PeMovieProfile_Frame() ((void)0)
#endif

#endif
