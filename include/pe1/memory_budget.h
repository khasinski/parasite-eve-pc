#ifndef PE1_MEMORY_BUDGET_H
#define PE1_MEMORY_BUDGET_H

#include <stddef.h>

/* Sizes of the large native buffers that replace PS1 memory regions.
 * The defaults are generous upper bounds for desktop hosts. Targets with
 * little RAM (PE_SMALL_MEMORY in CMake) pass smaller values; every buffer
 * below is bounds-checked where it is filled, and its high-water mark is
 * reported when PE_MEMORY_REPORT=1 is set in the environment. */

/* Matrix push depth in Render_TransformVertices. The command count is u16,
 * so 65536 covers any input; real skeletons nest only a few levels. */
#ifndef PE_RENDER_TRANSFORM_STACK_DEPTH
#define PE_RENDER_TRANSFORM_STACK_DEPTH 65536
#endif

/* Sets in the PGXP bone-matrix cache (4 ways each). Only affects hit rate. */
#ifndef PE_PGXP_BONE_CACHE_SETS
#define PE_PGXP_BONE_CACHE_SETS 1024 /* must be a power of two */
#endif

/* Room TIM workspace (D_8010BD00). Sized for the 12-bit sector count of a
 * room texture read; the same space later holds the mesh packet buffers. */
#ifndef PE_BOOT_TEXTURE_WORKSPACE_BYTES
#define PE_BOOT_TEXTURE_WORKSPACE_BYTES 0x800000
#endif

typedef enum PeMemoryCounter {
    PE_MEM_TRANSFORM_STACK_DEPTH,   /* deepest matrix push, in entries */
    PE_MEM_ROOM_TEXTURE_READ,       /* largest room texture read, bytes */
    PE_MEM_ROOM_SCRATCH_READ,       /* largest room scratch read, bytes */
    PE_MEM_ROOM_ASSET_READ,         /* largest room asset read, bytes */
    PE_MEM_MESH_PACKET_BYTES,       /* largest mesh packet set, bytes */
    PE_MEM_COUNTER_COUNT
} PeMemoryCounter;

/* Records a value and, with PE_MEMORY_REPORT=1, prints each new peak to
 * stderr together with the room that caused it. A capacity of 0 means the
 * buffer size is not known at this point. */
void PeMemory_Note(PeMemoryCounter counter, size_t value, size_t capacity);
/* Room number (M001...) shown next to later peaks; 0 when unknown. */
void PeMemory_SetRoom(int room);

#endif /* PE1_MEMORY_BUDGET_H */
