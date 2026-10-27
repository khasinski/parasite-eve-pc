static int original_save_metadata_storage(void)
{
    SaveRuntimeState saved=g_SaveRuntimeState;
    int index=g_SaveMetadataWindowIndex, length=g_SaveMetadataWindowLength;
    u8 *cursor=g_SaveMetadataCursor;
    ItemDataRecord *source=g_SaveMetadataSourceRecord;
    ItemDataRecord item={0};
    int valid=1;
    memset(&g_SaveRuntimeState,0xFF,sizeof(g_SaveRuntimeState));
    for(int window=0;window<2;++window) {
        g_SaveMetadataWindowIndex=window;
        Save_SelectMetadataWindow(NULL);
        if(g_SaveMetadataCursor!=g_SaveRuntimeState.windows.metadata[window].text ||
           g_SaveMetadataWindowLength!=8 || g_SaveMetadataSourceRecord) valid=0;
        Save_AppendMetadataChar(0x30+window);
        if(g_SaveRuntimeState.windows.metadata[window].text[0]!=0x30+window ||
           g_SaveRuntimeState.windows.metadata[window].text[1]!=0xFF) valid=0;
        item.kind=window?ITEM_KIND_ARMOR:0;
        Save_SelectMetadataWindow(&item);
        if(g_SaveMetadataCursor!=g_CursorRenderMetadataWindows[window].text ||
           g_SaveMetadataSourceRecord!=&item) valid=0;
        Save_AppendMetadataChar(0x40+window);
        if(((u8 *)&g_SaveRuntimeState)[0x12C4+window*16]!=0x40+window ||
           g_CursorRenderDataBlock[window*16]!=0x40+window) valid=0;
        /* Retail replaces the eighth glyph when a ninth is entered. */
        for(unsigned i=1;i<8;++i) Save_AppendMetadataChar(0x50+i);
        Save_AppendMetadataChar(0x60);
        if(g_CursorRenderMetadataWindows[window].text[7]!=0x60 ||
           g_CursorRenderMetadataWindows[window].text[8]!=0xFF) valid=0;
    }
    g_SaveRuntimeState=saved;
    g_SaveMetadataWindowIndex=index; g_SaveMetadataWindowLength=length;
    g_SaveMetadataCursor=cursor; g_SaveMetadataSourceRecord=source;
    if(!valid) fprintf(stderr,"Original save metadata storage/selection failed\n");
    return valid;
}

static char card_test_root[128];
static int card_test_path(char *destination,const char *source,int maximum)
{
    if(strncmp(source,"bu",2) || (source[2]!='0' && source[2]!='1') || source[3]!='0' || source[4]!=':') return -1;
    const char *name=source+5;
    return snprintf(destination,(size_t)maximum,"%s/bu%c0/%s",card_test_root,source[2],*name=='*'?"":name);
}
static int original_slot_compatibility(void)
{
    MemCardPortState saved[2];memcpy(saved,g_MemCardPortStates,sizeof(saved));
    MenuWidgetNode widget={0},*saved_widget=g_MenuActiveWidget;
    int saved_mode=g_McDialogMode,saved_style=g_SaveTitleStyleFlag;
    const int modes[]={0,1,2,-1},styles[]={0,1,255},states[]={0,1,2,3,255};
    const int counts[]={0,1,15},indices[]={-1,0,1,14,15};
    uint64_t hash=UINT64_C(14695981039346656037);unsigned cases=0;
    g_MenuActiveWidget=&widget;
    for(int port=0;port<3;++port)for(int mode=0;mode<4;++mode)
    for(int style=0;style<3;++style)for(int state=0;state<5;++state)
    for(int ready=0;ready<2;++ready)for(int count=0;count<3;++count)
    for(int slot=0;slot<5;++slot) {
        memset(g_MemCardPortStates,0,sizeof(saved));
        widget.selected_base=port+0x25;g_McDialogMode=modes[mode];g_SaveTitleStyleFlag=styles[style];
        int index=indices[slot];
        if(port<2) {
            g_MemCardPortStates[port].fileCount=counts[count];
            if(index>=0 && index<15) {
                MemCardSaveSlot *entry=&g_MemCardPortStates[port].slots[index];
                entry->state=states[state];entry->metadataReady=ready;entry->titleStyleFlag=1;
            }
        }
        u32 result=Menu_IsMemCardSlotCompatible(index);
        for(int byte=0;byte<4;++byte)hash=(hash^((result>>(byte*8))&255))*UINT64_C(1099511628211);
        ++cases;
    }
    memcpy(g_MemCardPortStates,saved,sizeof(saved));g_MenuActiveWidget=saved_widget;
    g_McDialogMode=saved_mode;g_SaveTitleStyleFlag=saved_style;
    if(cases!=5400 || hash!=UINT64_C(0x6d2dc156bb7a40e5))return 0;
    puts("Original save-slot compatibility: 5400 USA CPU cases including pending metadata passed");
    return 1;
}

/* Drive the real asynchronous manager in private directories on both ports. */
static int original_atomic_card_write(unsigned port, const char *host) {
    MemCardPortState saved = g_MemCardPortStates[port];
    MemCardPortState *active = g_MemCardActiveState;
    int loaded = g_MemCardLoadSucceeded, style = g_SaveTitleStyleFlag;
    int queued = g_AkaoMessageQueueCount, pending = g_McOpPending;
    size_t bytes = 0;
    u8 *old = SDL_LoadFile(host, &bytes);
    if (!old || bytes != 0x2000) { SDL_free(old); return 0; }
    u8 replacement[0x2000];
    for (unsigned i = 0; i < sizeof(replacement); ++i) replacement[i] = old[i] ^ 0x5A;
    int valid = 1;
    for (unsigned fault = 0; fault < 6 && valid; ++fault) {
        MemCardPortState *state = &g_MemCardPortStates[port];
        memset(state, 0, sizeof(*state));
        state->present = 1; state->selectedSlot = fault >= 4 ? 13 : 14;
        state->slots[14].state = MEMCARD_SLOT_OCCUPIED;
        state->slots[14].titleStyleFlag = port;
        state->fileDescriptor = -1; state->managerState = fault >= 4 ? 4 : 11;
        state->transferSize = 0x2000; state->retryCount = 30;
        g_SaveTitleStyleFlag = fault == 1 || fault == 3 ? !port : port;
        g_MemCardLoadSucceeded = 0; g_MemCardActiveState = state;
        memcpy(g_MemCardFileBuffer, replacement, sizeof(replacement));
        MenuWidget_InitPool(); g_MenuDeferredCallback = NULL;
        char blocked[4096] = "", logical[32];
        if (fault == 1) {
            snprintf(logical, sizeof(logical), g_MemCardFilenameFormatPtr, port, '0' + !port, 'O');
            Psyz_AdjustPath(blocked, logical, sizeof(blocked));
            if (!SDL_CreateDirectory(blocked)) { valid = 0; break; }
        }
        MemCard_UpdateSaveState(port); /* remember old name */
        MemCard_UpdateSaveState(port); /* create private staging file */
        MemCard_UpdateSaveState(port); /* queue original 1 KiB writes */
        MemCard_UpdateSaveState(port); /* first partial write */
        u8 *check = SDL_LoadFile(host, &bytes);
        if (state->managerState != 9 || !check || bytes != 0x2000 || memcmp(check, old, bytes)) valid = 0;
        SDL_free(check);
        if (!fault || fault == 4) {
            state->present = 0; MemCard_UpdateSaveState(port); /* interrupted */
        } else {
            for (unsigned i = 0; i < 8 && state->managerState == 9; ++i) MemCard_UpdateSaveState(port);
            if ((fault == 1 && state->managerState == 3) ||
                (fault != 1 && state->managerState != 3)) valid = 0;
        }
        const char *published = host;
        char changed[4096], created[4096] = "";
        if (fault >= 4) {
            snprintf(logical, sizeof(logical), g_MemCardFilenameFormatPtr, port, '0' + port, 'N');
            Psyz_AdjustPath(created, logical, sizeof(created));
            SDL_PathInfo info;
            if (fault == 4 && SDL_GetPathInfo(created, &info)) valid = 0;
            if (fault == 5) published = created;
        }
        if (fault == 3) {
            Psyz_AdjustPath(changed, g_MemCardFilePath, sizeof(changed)); published = changed;
            SDL_PathInfo info; if (SDL_GetPathInfo(host, &info)) valid = 0;
        }
        check = SDL_LoadFile(published, &bytes);
        if (!check || bytes != 0x2000 || memcmp(check, fault < 2 || fault == 4 ? old : replacement, bytes)) valid = 0;
        SDL_free(check);
        if (fault == 5 && !SDL_RemovePath(created)) valid = 0;
        if (*blocked && !SDL_RemovePath(blocked)) valid = 0;
        if (fault == 3 && !SDL_RenamePath(changed, host)) valid = 0;
        if (!SDL_SaveFile(host, old, 0x2000)) valid = 0;
        MenuWidget_InitPool(); g_MenuDeferredCallback = NULL;
    }
    /* A staging file left by a killed process must not consume a PS1 card
     * block or become a selectable save on the next directory scan. */
    char orphan[4096];
    const char *slash = strrchr(host, '/');
    snprintf(orphan, sizeof(orphan), "%.*s.pe-save-leftover.tmp", (int)(slash + 1 - host), host);
    if (!SDL_SaveFile(orphan, old, 0x2000)) valid = 0;
    int context = g_MemCardReadContext;
    g_MemCardReadContext = 0;
    g_MemCardPortStates[port].managerState = 2; g_MemCardPortStates[port].present = 1;
    MemCard_UpdateSaveState(port);
    if (g_MemCardPortStates[port].sequence != 1) valid = 0;
    g_MemCardReadContext = context;
    if (!SDL_RemovePath(orphan)) valid = 0;
    SDL_free(old);
    g_MemCardPortStates[port] = saved; g_MemCardActiveState = active;
    g_MemCardLoadSucceeded = loaded; g_SaveTitleStyleFlag = style;
    g_AkaoMessageQueueCount = queued; g_McOpPending = pending;
    if (valid) printf("Atomic card port %u: partial abort, failed rename, overwrite, clear-data filename switch, first save and orphan staging scan passed\n", port);
    return valid;
}

static int original_card_file_services(const u8 *image);

static int native_save_hook_safety(void);

static int native_save_safety(void) {
    /* Minimal synthetic text data lets the real save-manager dialogs run
     * without shipping or requiring any retail assets in desktop CI. */
    ResidentDataPack saved = g_ResidentDataPack;
    memset(&g_ResidentDataPack, 0, sizeof(g_ResidentDataPack));
    g_ResidentDataPack.directory.text = 0x30;
    TextTableDirectory *directory = (TextTableDirectory *)(g_ResidentDataPack.bytes + 0x30);
    directory->header.tableOffsets[0] = 0x20;
    TextOffsetTable *table = (TextOffsetTable *)(directory->bytes + 0x20);
    table->header.count = 128;
    for (unsigned i = 0; i < 128; ++i) table->header.offsets[i] = 0x102;
    table->bytes[0x102] = 0xFF;
    u8 image[0x2000];
    for (unsigned i = 0; i < sizeof(image); ++i) image[i] = (u8)(i * 37);
    int valid = original_card_file_services(image);
    SaveRuntimeState candidate = {0};
    candidate.player.aya.inventory_slot_count = 50;
    candidate.player.aya.equipped_weapon_slot = 49;
    candidate.player.aya.equipped_armor_slot = 48;
    candidate.player.aya.inventory_items[49] = 0x100;
    candidate.player.aya.inventory_items[48] = 0x101;
    candidate.player.runtime.inventory.equipment[0] = (ItemDataRecord){
        .iconId=1,.itemId=0x78,.kind=1,.tailCount=10,.tailCapacity=10,.bonusStats={999,999,999}};
    candidate.player.runtime.inventory.equipment[1] = (ItemDataRecord){
        .iconId=2,.itemId=0xB7,.kind=9,.tailCount=10,.tailCapacity=10};
    if (!Inv_ValidateSavedInventory(&candidate)) valid = 0;
    SaveRuntimeState clean = candidate;
    for (unsigned fault = 0; fault < 8; ++fault) {
        candidate = clean;
        switch (fault) {
        case 0: candidate.player.runtime.inventory.equipment[0].kind = 255; break;
        case 1: candidate.player.runtime.inventory.equipment[0].tailCount = 11; break;
        case 2: candidate.player.runtime.inventory.equipment[1].tailCapacity = 255; break;
        case 3: candidate.player.aya.equipped_weapon_slot = -128; break;
        case 4: candidate.player.aya.equipped_armor_slot = 50; break;
        case 5: candidate.player.aya.inventory_slot_count = 51; break;
        case 6: candidate.player.runtime.inventory.storage[99] = 0x180; break;
        case 7: candidate.player.runtime.inventory.specialStorage[81] = 0x209; break;
        }
        if (Inv_ValidateSavedInventory(&candidate)) valid = 0;
    }
    /* Run the real sorter and selection-mask rebuild too, including the
     * corrupt kind that previously indexed beyond the rank table. */
    g_SaveRuntimeState = clean;
    g_InventoryRuntime.equipment[0].kind = 255;
    g_AyaSaveState.equipped_weapon_slot = -128;
    g_AyaSaveState.equipped_armor_slot = 127;
    g_InvBonusSlotCount = 0;
    Inv_SortInventoryByMode(1, 0);
    if (g_AyaSaveState.equipped_weapon_slot != -1 || g_AyaSaveState.equipped_armor_slot != -1) valid = 0;
    if (!native_task_relocation_safety() || !native_save_hook_safety()) valid = 0;
    g_ResidentDataPack = saved;
    return valid ? 0 : 1;
}

static int original_card_file_services(const u8 *image)
{
    if(!original_slot_compatibility())return 0;
    snprintf(card_test_root,sizeof(card_test_root),"pe-card-io-%llx",(unsigned long long)SDL_GetPerformanceCounter());
    SDL_PathInfo info;
    if(SDL_GetPathInfo(card_test_root,&info) || !SDL_CreateDirectory(card_test_root)) return 0;
    int valid=1;
    u8 expected[0x2000],actual[0x2000];
    if(image)memcpy(expected,image,sizeof(expected));
    else for(unsigned i=0;i<sizeof(expected);++i)expected[i]=(u8)(i*37+(i>>8));
    Psyz_AdjustPathCB(card_test_path);
    for(unsigned port=0;port<2;++port) {
        char directory[160],host[256];
        snprintf(directory,sizeof(directory),"%s/bu%u0",card_test_root,port);
        if(!SDL_CreateDirectory(directory)) {valid=0;break;}
        snprintf(g_MemCardFilePath,sizeof(g_MemCardFilePath),g_MemCardFilenameFormatPtr,port,'0'+port,'O');
        if(strlen(g_MemCardFilePath)!=25 || strlen(g_MemCardFilePath+5)!=20) valid=0;
        int fd=open(g_MemCardFilePath,0x10200);
        if(fd<0) {valid=0;break;}
        close(fd);
        fd=open(g_MemCardFilePath,2);
        if(fd<0) {valid=0;break;}
        memcpy(g_MemCardFileBuffer,expected,sizeof(expected));
        if(write(fd,(char *)g_MemCardFileBuffer,sizeof(expected))!=sizeof(expected)) valid=0;
        if(close(fd)) valid=0;
        fd=open(g_MemCardFilePath,1);
        if(fd<0) {valid=0;break;}
        if(read(fd,actual,sizeof(actual))!=sizeof(actual) || memcmp(actual,expected,sizeof(actual))) valid=0;
        if(lseek(fd,0x1FF0,0)!=0x1FF0 || read(fd,actual,16)!=16 || memcmp(actual,expected+0x1FF0,16)) valid=0;
        if(read(fd,actual,1)!=0) valid=0;
        close(fd);
        fd=open(g_MemCardFilePath,3);
        if(fd<0) {valid=0;break;}
        u8 marker=0xC7;
        if(lseek(fd,0x123,0)!=0x123 || write(fd,(char *)&marker,1)!=1 ||
           lseek(fd,0x123,0)!=0x123 || read(fd,actual,1)!=1 || actual[0]!=marker) valid=0;
        close(fd);
        Psyz_AdjustPath(host,g_MemCardFilePath,sizeof(host));
        if (image && !original_atomic_card_write(port, host)) valid = 0;
        snprintf(g_MemCardFilePath,sizeof(g_MemCardFilePath),g_MemCardFilenameFormatPtr,port,'0'+port,'O');
        char search[8];snprintf(search,sizeof(search),"bu%u0:*",port);
        MemCardDirEntry entry;
        memset(&entry,0xA5,sizeof(entry));
        if(!firstfile(search,&entry) || entry.size!=0x2000 || memcmp(entry.name,g_MemCardFilePath+5,20) || nextfile(&entry)) valid=0;
        Psyz_AdjustPath(host,g_MemCardFilePath,sizeof(host));
        if(!SDL_RemovePath(host) || !SDL_RemovePath(directory))valid=0;
    }
    if(open("bu00:missing-file",1)>=0 || read(-1,actual,1)!=-1 || write(-1,(char *)actual,1)!=-1)valid=0;
    Psyz_AdjustPathCB(NULL);
    if(!SDL_RemovePath(card_test_root)) valid=0;
    memset(g_MemCardFileBuffer,0,sizeof(g_MemCardFileBuffer));
    if(valid)puts("Original card file ABI: both ports, exact 20-byte names, 8 KiB write/reopen/read, seek/update, directory entries and I/O errors passed");
    return valid;
}

void MemCard_InitManager(void);
static int original_system_start(void)
{
    g_MemCardPortStates[0].present=0xA5; g_MemCardPortStates[1].present=0xA5;
    Boot_InitSystem();
    if(!g_MemCardManagerReady || g_MemCardPortStates[0].present || g_MemCardPortStates[1].present ||
       g_PadPackets[0].header!=0x4100 || g_PadPackets[0].buttons!=0xFFFF ||
       g_PadPackets[0].leftX!=128 || g_PadPackets[0].rightY!=128 ||
       g_PadPackets[1].header!=0x00FF) return 0;
    long handles[MEMCARD_EVENT_COUNT]; memcpy(handles,g_MemCardEventHandles,sizeof(handles));
    int *flags[]={&g_MemCardEventF400Spec0004Flag,&g_MemCardRemovedEventPending,
        &g_MemCardRemovedEventPending,&g_MemCardEventF400Spec2000Flag,
        &g_MemCardEventF000Spec0004Flag,&g_MemCardEventF000Spec8000Flag,
        &g_MemCardEventF000Spec8000Flag,&g_MemCardEventF000Spec2000Flag};
    const unsigned specs[]={4,0x8000,0x100,0x2000};
    for(unsigned i=0;i<MEMCARD_EVENT_COUNT;++i) {
        if(handles[i]==-1) return 0;
        for(unsigned j=0;j<i;++j) if(handles[i]==handles[j]) return 0;
        for(unsigned j=0;j<MEMCARD_EVENT_COUNT;++j) *flags[j]=0;
        DeliverEvent(i<4?0xF4000001u:0xF0000011u,specs[i%4]);
        if(*flags[i]!=1) return 0;
    }
    g_MemCardPortStates[0].present=g_MemCardPortStates[1].present=1;
    MemCard_InitManager();
    if(memcmp(handles,g_MemCardEventHandles,sizeof(handles)) ||
       g_MemCardPortStates[0].present || g_MemCardPortStates[1].present) return 0;
    memset(g_MemCardPortStates,0xA5,2*sizeof(MemCardPortState));
    g_MemCardServicedPort=g_MemCardInfoPollCountdown=g_MemCardConnectDebounce=7;
    g_MemCardConnectPending[0]=g_MemCardConnectPending[1]=7;
    g_McOpPending=g_MemCardActivePortOneBased=g_MemCardSavePollTimeout=7;
    g_MemCardActivePromptPending=g_SaveTitleStyleFlag=g_MemCardLoadSucceeded=g_MemCardReadContext=7;
    g_MemCardActiveState=&g_MemCardPortStates[1];
    MemCard_InitState();
    MemCardPortState expected_ports[2];memset(expected_ports,0,sizeof(expected_ports));
    expected_ports[0].fileDescriptor=expected_ports[1].fileDescriptor=-1;
    if(memcmp(g_MemCardPortStates,expected_ports,sizeof(expected_ports)) || g_MemCardServicedPort ||
       g_MemCardInfoPollCountdown || g_MemCardConnectDebounce || g_MemCardConnectPending[0] || g_MemCardConnectPending[1] ||
       g_McOpPending || g_MemCardActivePortOneBased || g_MemCardSavePollTimeout || g_MemCardActivePromptPending ||
       g_SaveTitleStyleFlag || g_MemCardLoadSucceeded || g_MemCardReadContext || g_MemCardActiveState ||
       (u8 *)&g_MemCardSaveStateBuffer!=g_MemCardFileBuffer+0x100 || g_MemCardIconPalette!=(u8 *)g_MemCardIconHeader.palette)
        return 0;
    if(!original_card_file_services(NULL)) return 0;
    puts("Original system start: GPU/GTE/SPU, native pad reset and eight real card-event callbacks passed");
    return 1;
}

void Draw_AllocPrimRectFull(s32 x,s32 y,s32 width,s32 height);
void Draw_AllocPrimRect(void);
static int original_menu_draw_areas(void)
{
    static _Alignas(16) u8 arena[0x4000+32];
    u8 *saved_cursor=g_DrawPacketCursor,*saved_arena=g_DrawPacketArenaBase;
    u32 *saved_ordering=g_DrawOrderingTableEntry;
    int saved_slot=g_DrawBufferIndex,valid=1;
    MenuWidgetNode *saved_head=g_MenuWidgetActiveListHead;
    MenuWidgetNode parent={0},node={0};
    parent.x=17;parent.y=19;parent.children[0]=&node;
    node.x=11;node.y=13;node.draw_state=7;node.grid_width=5;
    node.disabled=9;node.visible_rows=3;
    g_MenuWidgetActiveListHead=&parent;
    for(unsigned slot=0;slot<2;++slot) {
        RenderNativeTag ordering={0xFFFFFF,0x1234};
        memset(arena,0xA5,sizeof(arena));
        g_DrawPacketArenaBase=arena;g_DrawPacketCursor=arena;
        g_DrawOrderingTableEntry=(u32 *)&ordering;g_DrawBufferIndex=slot;
        Draw_AllocPrimRectFull(21,33,89,41);
        Draw_AllocPrimRect();
        Draw_AllocPrimWithMask(&node);
        const unsigned rects[3][4]={{21,33,89,41},{0,0,320,224},{28,32,35,27}};
        for(unsigned i=0;i<3;++i) {
            GpuCmdPacket *packet=(GpuCmdPacket *)(arena+i*sizeof(*packet));
            unsigned x=rects[i][0],y=rects[i][1]+slot*224;
            unsigned lastx=x+rects[i][2]-1,lasty=y+rects[i][3]-1;
            if(packet->length!=2 || packet->field4!=(0xE3000000u|(y<<10)|x) ||
               packet->field8!=(0xE4000000u|(lasty<<10)|lastx) ||
               packet->u0.tag!=(i?(uintptr_t)(arena+(i-1)*sizeof(*packet)):0xFFFFFF)) valid=0;
        }
        uintptr_t head=(uintptr_t)(arena+2*sizeof(GpuCmdPacket));
        if(ordering.address!=head || ordering.length!=0x1234 ||
           g_DrawPacketCursor!=arena+3*sizeof(GpuCmdPacket)) valid=0;
        g_DrawPacketCursor=arena+0x4000-sizeof(GpuCmdPacket);
        Draw_AllocPrimRectFull(1,2,3,4);Draw_AllocPrimRect();Draw_AllocPrimWithMask(&node);
        if(ordering.address!=head || ordering.length!=0x1234 ||
           g_DrawPacketCursor!=arena+0x4000-sizeof(GpuCmdPacket)) valid=0;
        for(size_t i=3*sizeof(GpuCmdPacket);i<sizeof(arena);++i) if(arena[i]!=0xA5) valid=0;
    }
    g_DrawPacketCursor=saved_cursor;g_DrawPacketArenaBase=saved_arena;
    g_DrawOrderingTableEntry=saved_ordering;g_DrawBufferIndex=saved_slot;
    g_MenuWidgetActiveListHead=saved_head;
    if(!valid) fprintf(stderr,"Original menu draw-area payload/native links/arena guard failed\n");
    return valid;
}

void Draw_EmitDigitSprite(int digit);
static int original_digit_packets(void)
{
    /* Payload hashes from USA Draw_SetFontVariant/Draw_EmitDigitSprite
     * 8005F844/8005F874, executed in MIPS, excluding the platform DMA tag. */
    static const uint64_t hashes[2][2]={{UINT64_C(0x4453c0728d2e749d),UINT64_C(0x22072416028e569d)},
        {UINT64_C(0xdc84dc2c95ba4c69),UINT64_C(0x338cd32cbc4986f1)}};
    const int positions[3][2]={{23,45},{-17,-9},{65533,65534}};
    const int digits[]={-1,-10,0,1,9,10,79,INT_MAX};
    static _Alignas(16) u8 arena[0x4000+16];
    u8 *cursor=g_DrawPacketCursor,*base=g_DrawPacketArenaBase;
    u32 *entry=g_DrawOrderingTableEntry;
    int x=g_TextCursorX,y=g_TextCursorY,dim=g_DrawTextDimmed;
    u32 color=g_DrawPrimColor,shaded=g_DrawColorShaded;
    int clut=g_DrawDigitFontTpageClut,u=g_DrawDigitFontBaseTexU,v=g_DrawDigitFontBaseTexV;
    int valid=1;
    for(unsigned font=0;font<2;++font) for(unsigned dark=0;dark<2;++dark) {
        RenderNativeTag ordering={0xFFFFFF,0x9876};
        uintptr_t previous=ordering.address;
        memset(arena,0xA5,sizeof(arena));
        g_DrawPacketCursor=g_DrawPacketArenaBase=arena;g_DrawOrderingTableEntry=(u32 *)&ordering;
        Draw_SetFontVariant(font);Draw_SetColor(0xE1A25C);Draw_SetTextDimmed(dark);
        uint64_t hash=UINT64_C(14695981039346656037);
        for(unsigned pos=0;pos<3;++pos) for(unsigned d=0;d<sizeof(digits)/sizeof(digits[0]);++d) {
            Draw_SetCursor(positions[pos][0],positions[pos][1]);
            RenderTexturedQuad *quad=(RenderTexturedQuad *)g_DrawPacketCursor;
            Draw_EmitDigitSprite(digits[d]);
            if(quad->tag.length!=9 || quad->tag.address!=previous ||
               ordering.address!=(uintptr_t)quad || ordering.length!=0x9876 ||
               g_DrawPacketCursor!=(u8 *)quad+sizeof(*quad) ||
               g_TextCursorX!=positions[pos][0] || g_TextCursorY!=positions[pos][1]) valid=0;
            previous=(uintptr_t)quad;
            const u8 *payload=(const u8 *)quad+sizeof(RenderNativeTag);
            for(unsigned i=0;i<36;++i) hash=(hash^payload[i])*UINT64_C(1099511628211);
        }
        if(hash!=hashes[font][dark]) valid=0;
        for(size_t i=24*sizeof(RenderTexturedQuad);i<sizeof(arena);++i) if(arena[i]!=0xA5) valid=0;
        g_DrawPacketCursor=arena+0x4000-sizeof(RenderTexturedQuad);
        Draw_EmitDigitSprite(7);
        if(ordering.address!=previous || ordering.length!=0x9876 ||
           g_DrawPacketCursor!=arena+0x4000-sizeof(RenderTexturedQuad)) valid=0;
    }
    g_DrawPacketCursor=cursor;g_DrawPacketArenaBase=base;g_DrawOrderingTableEntry=entry;
    g_TextCursorX=x;g_TextCursorY=y;g_DrawTextDimmed=dim;g_DrawPrimColor=color;g_DrawColorShaded=shaded;
    g_DrawDigitFontTpageClut=clut;g_DrawDigitFontBaseTexU=u;g_DrawDigitFontBaseTexV=v;
    if(!valid) fprintf(stderr,"Original digit payload/native links differ from USA MIPS reference\n");
    return valid;
}

static int original_color_quad_packets(void)
{
    static _Alignas(16) u8 arena[0x4000+32];
    u8 *cursor=g_DrawPacketCursor,*base=g_DrawPacketArenaBase;u32 *entry=g_DrawOrderingTableEntry;
    u32 flags=g_GameStateFlags,color=g_DrawPrimColor,shaded=g_DrawColorShaded;int dim=g_DrawTextDimmed;
    const int widths[]={-32768,-1,0,1,7,28,55,56,57,1000,32767};
    const int heights[]={-1,0,1,7,28,56,1000,32767};
    size_t bytes=2*sizeof(RenderGouraudQuad)+sizeof(RenderColorTilePacket)+sizeof(RenderDrawModePacket);
    uint64_t hash=UINT64_C(14695981039346656037);int valid=1;
    for(unsigned flag=0;flag<2;++flag)for(unsigned w=0;w<11;++w)for(unsigned h=0;h<8;++h) {
        memset(arena,0xA5,sizeof(arena));RenderNativeTag ordering={0xFFFFFF,0xAB};
        g_DrawPacketArenaBase=g_DrawPacketCursor=arena;g_DrawOrderingTableEntry=(u32 *)&ordering;
        g_GameStateFlags=flag*2;Draw_AllocColorQuad(widths[w],heights[h]);
        RenderGouraudQuad *first=(void *)arena,*second=(void *)(arena+sizeof(*first));
        RenderColorTilePacket *tile=(void *)(arena+2*sizeof(*first));
        RenderDrawModePacket *mode=(void *)((u8 *)tile+sizeof(*tile));
        if(g_DrawPacketCursor!=arena+bytes || ordering.address!=(uintptr_t)mode || ordering.length!=0xAB ||
           first->tag.address!=0xFFFFFF || first->tag.length!=8 || second->tag.address!=(uintptr_t)first || second->tag.length!=8 ||
           tile->tag.address!=(uintptr_t)second || tile->tag.length!=3 || mode->tag.address!=(uintptr_t)tile || mode->tag.length!=1 ||
           g_DrawPrimColor!=0x808080 || g_DrawColorShaded!=0x404040 || g_DrawTextDimmed)valid=0;
        const u8 *payloads[]={(u8 *)first+sizeof(RenderNativeTag),(u8 *)second+sizeof(RenderNativeTag),
                             (u8 *)tile+sizeof(RenderNativeTag),(u8 *)&mode->command};
        const unsigned sizes[]={32,32,12,4};
        for(unsigned p=0;p<4;++p)for(unsigned i=0;i<sizes[p];++i)hash=(hash^payloads[p][i])*UINT64_C(1099511628211);
        for(unsigned i=bytes;i<sizeof(arena);++i)if(arena[i]!=0xA5)valid=0;
    }
    /* All four GPU payloads from 176 executions of USA 80061044 in MIPS. */
    if(hash!=UINT64_C(0xa60c172cc58948fd)) {
        fprintf(stderr,"Original color quad hash %016llx differs from USA MIPS\n",(unsigned long long)hash);valid=0;
    }
    for(unsigned remaining=0;remaining<=bytes+8;remaining+=8) {
        memset(arena,0xA5,sizeof(arena));RenderNativeTag ordering={0xFFFFFF,0xAB};
        u8 *start=arena+0x4000-remaining;
        g_DrawPacketArenaBase=arena;g_DrawPacketCursor=start;g_DrawOrderingTableEntry=(u32 *)&ordering;
        g_DrawPrimColor=0x123456;g_DrawColorShaded=0x654321;g_DrawTextDimmed=1;
        Draw_AllocColorQuad(28,56);
        if(remaining<=bytes) {
            if(g_DrawPacketCursor!=start || ordering.address!=0xFFFFFF || ordering.length!=0xAB ||
               g_DrawPrimColor!=0x123456 || g_DrawColorShaded!=0x654321 || g_DrawTextDimmed!=1)valid=0;
            for(unsigned i=0;i<sizeof(arena);++i)if(arena[i]!=0xA5)valid=0;
        } else {
            if(g_DrawPacketCursor!=start+bytes)valid=0;
            for(u8 *p=arena;p<start;++p)if(*p!=0xA5)valid=0;
            for(u8 *p=start+bytes;p<arena+sizeof(arena);++p)if(*p!=0xA5)valid=0;
        }
    }
    /* Feed the same native chain to PsyZ and observe VRAM headlessly.
     * Check the complete footprint, background blend and all gauge pixels. */
    static u16 saved_pixels[320*240],pixels[320*240];
    DRAWENV old_draw,draw;RECT area={0,0,320,240};
    GetDrawEnv(&old_draw);StoreImage(&area,saved_pixels);DrawSync(0);
    SetDefDrawEnv(&draw,0,0,320,240);draw.isbg=0;draw.dtd=0;PutDrawEnv(&draw);
    for(unsigned flag=0;flag<2;++flag)for(unsigned fill=0;fill<3;++fill) {
        RenderNativeTag ordering={0xFFFFFF,0};memset(arena,0xA5,sizeof(arena));
        g_DrawPacketArenaBase=g_DrawPacketCursor=arena;g_DrawOrderingTableEntry=(u32 *)&ordering;
        g_GameStateFlags=flag*2;Draw_AllocColorQuad(fill*28,56);
        ClearImage(&area,0,0,0);DrawOTag(&ordering);DrawSync(0);
        StoreImage(&area,pixels);DrawSync(0);
        int offset=flag?0:7;
        for(int y=0;y<240;++y)for(int x=0;x<320;++x) {
            int background=x>=221 && x<305 && y>=166+offset && y<177+offset;
            int gauge=x>=229 && x<285 && y>=170+offset && y<173+offset;
            u16 value=pixels[y*320+x];
            if((!background && value) || (background && !gauge && value!=0x0C63) ||
               (gauge && (!value || value==0x0C63))) {
                if(valid)fprintf(stderr,"Original color gauge VRAM (%d,%d)=%04x, background=%d gauge=%d\n",x,y,value,background,gauge);
                valid=0;
            }
        }
    }
    LoadImage(&area,saved_pixels);PutDrawEnv(&old_draw);DrawSync(0);
    g_DrawPacketCursor=cursor;g_DrawPacketArenaBase=base;g_DrawOrderingTableEntry=entry;
    g_GameStateFlags=flags;g_DrawPrimColor=color;g_DrawColorShaded=shaded;g_DrawTextDimmed=dim;
    if(valid)puts("Original color gauge: 176 four-command payloads match USA MIPS, native links, arena limits and six complete headless VRAM footprints passed");
    return valid;
}


/* USA 8003495C/80034DE0, including the real wipe callees: 18,560 cases.
 * Compare the two numbers referenced by the HP glyph stream. Retail reads
 * past its local terminator into unused number slots; those are excluded. */

/* Execute the original parser and real SDK packet helpers, comparing state
 * and every linked command against unhooked USA 371B0/37870. */

/* CPU algorithm from USA 8007041C with COP2 forwarded to the same PsyZ GTE;
 * this isolates CPU/layout fidelity, not independent hardware GTE accuracy. */



static int original_escape_chance(PeDisc *disc,PeDiscFile *exe)
{
    u8 messages[62];
    if(!pe_disc_read(disc,exe,0x9159C-0x10000+0x800,messages,34) || memcmp(messages,g_EscapeFailedTexts,34) ||
       !pe_disc_read(disc,exe,0x915C0-0x10000+0x800,messages,28) || memcmp(messages,g_EscapeBlockedTexts,28))return 0;
    Combatant active={0};BattleEntity entities[4]={{0}};EnemyCombatant enemies[3]={{0}};
    Combatant *saved_actor=g_ActiveActor;FieldActor *saved_player=g_PlayerEntity,*saved_head=g_FieldActorListHead;
    u8 saved_overlay=g_BattleSaveOverlayActive,*saved_message=g_CurItemEffectData;int saved_window=g_SaveMetadataWindowIndex;
    u8 custom=255;u8 *texts[]={&custom,g_EscapeFailedTexts,g_EscapeFailedTexts+17,g_EscapeBlockedTexts,g_EscapeBlockedTexts+14};
    const u8 player_ranks[]={0,1,2,127,128,254,255},enemy_ranks[]={0,1,2,126,127,128,255};
    const u16 health[][2]={{0,100},{9,100},{10,100},{65535,65535},{32767,32768},{100,100}};
    const u32 seeds[]={0x12345678,0x87654321};
    g_ActiveActor=&active;g_PlayerEntity=g_FieldActorListHead=(FieldActor *)&entities[0];
    uint64_t hash=UINT64_C(14695981039346656037);unsigned cases=0;int valid=1;
#define ESCAPE_PUT(value,bytes) do {uint64_t v_=(uint64_t)(value); \
    for(unsigned b_=0;b_<(bytes);++b_){hash=(hash^(u8)v_)*UINT64_C(1099511628211);v_>>=8;}}while(0)
    for(unsigned rank=0;rank<7;++rank)for(unsigned enemy=0;enemy<7;++enemy)
    for(unsigned hp=0;hp<6;++hp)for(unsigned attempts=0;attempts<8;++attempts)
    for(unsigned scenario=0;scenario<6;++scenario)for(unsigned window=0;window<2;++window)for(unsigned seed=0;seed<2;++seed){
        memset(entities,0,sizeof(entities));memset(enemies,0,sizeof(enemies));
        entities[0].core=(EnemyCombatant *)&active;entities[0].next=scenario==0?NULL:&entities[1];
        entities[1].next=&entities[2];entities[2].next=&entities[3];
        for(unsigned i=0;i<3;++i){entities[i+1].core=&enemies[i];enemies[i].hpAlive=1;}
        enemies[0].field04.bytes.rank=enemy_ranks[enemy];enemies[1].field04.bytes.rank=127;enemies[2].field04.bytes.rank=128;
        if(scenario==1){enemies[1].hpAlive=0;enemies[1].statusFlags2=0x40000;entities[3].core=NULL;}
        if(scenario==3){enemies[0].hpAlive=0;enemies[0].statusFlags2=0x40000;enemies[1].field04.bytes.rank=2;entities[3].core=NULL;}
        if(scenario==4)enemies[0].statusFlags2=0x40000;
        if(scenario==5){entities[1].core=NULL;enemies[1].hpAlive=-1;enemies[1].statusFlags2=0x40000;
            enemies[2].field04.bytes.rank=enemy_ranks[enemy];enemies[2].statusFlags2=0x40000;}
        active.field04.bytes.rank=player_ranks[rank];active.curHP=health[hp][0];active.maxHP=health[hp][1];
        active.stateFlags=(s32)((0xA13579BDu&0xF1FFFFFFu)|(attempts<<25));
        g_SaveMetadataWindowIndex=window;g_BattleSaveOverlayActive=7;g_CurItemEffectData=&custom;
        srand(seeds[seed]);int result=Battle_RollEscapeChance();int next_random=rand();
        unsigned message=5;for(unsigned i=0;i<5;++i)if(g_CurItemEffectData==texts[i])message=i;
        if(message==5)valid=0;
        ESCAPE_PUT(result,4);ESCAPE_PUT(active.stateFlags,4);ESCAPE_PUT(g_BattleSaveOverlayActive,1);ESCAPE_PUT(message,1);ESCAPE_PUT(next_random,2);++cases;
    }
#undef ESCAPE_PUT
    g_ActiveActor=saved_actor;g_PlayerEntity=saved_player;g_FieldActorListHead=saved_head;
    g_BattleSaveOverlayActive=saved_overlay;g_CurItemEffectData=saved_message;g_SaveMetadataWindowIndex=saved_window;
    if(!valid || hash!=UINT64_C(0x02675eb74a42ca49)){
        fprintf(stderr,"Original escape chance: cases=%u hash=%016llx valid=%d\n",cases,(unsigned long long)hash,valid);return 0;}
    puts("Original escape chance: 56448 USA cases, rank/HP boundaries, signed chance narrowing, attempts, blocked/dead/null cores, both text variants and RNG consumption match");return 1;
}

static int original_action_slot(void)
{
    Combatant active={0};BattleAction action={0};active.action=&action;
    BattleEntity entities[4]={{0}};EnemyCombatant state={0},peer_core={0},player_core={0};RenderObjectHeader header={0};
    Combatant *saved_active=g_ActiveActor;FieldActor *saved_player=g_PlayerEntity,*saved_head=g_FieldActorListHead;
    BattleEntity *saved_actors[7];memcpy(saved_actors,g_BattleActionActors,sizeof(saved_actors));
    BattleInitSlot saved_queue=g_BattleActionQueue[0];
    u8 saved_count=g_BattleActionActorCount,saved_fade=g_BattleFadeLevel,saved_index=g_BattleActionIndex;
    int saved_slot=g_ActiveDrawSlot;g_ActiveActor=&active;
    g_FieldActorListHead=g_PlayerEntity=(FieldActor *)&entities[3];
    const u32 phases[]={0,0x2000,0x4000,0x6000};const int kinds[]={0,1,2,4,-1},health[]={-1,0,1};
    const unsigned fades[]={0,1,7,8,127,128,129,135,136,255},commands[]={0,0x189,0x196};
    const unsigned arrays[][2]={{0,0},{0,3},{0,7},{1,1},{6,6},{6,7}};
    uint64_t hash=UINT64_C(14695981039346656037);unsigned cases=0;int valid=1;
#define SLOT_PUT(value,bytes) do {uint64_t v_=(uint64_t)(value); \
    for(unsigned b_=0;b_<(bytes);++b_){hash=(hash^(u8)v_)*UINT64_C(1099511628211);v_>>=8;}}while(0)
    for(unsigned phase=0;phase<4;++phase)for(unsigned kind=0;kind<5;++kind)for(unsigned alive=0;alive<3;++alive)
    for(unsigned fade=0;fade<10;++fade)for(unsigned command=0;command<3;++command)for(unsigned a=0;a<2;++a)
    for(unsigned array=0;array<6;++array)for(unsigned peer=0;peer<3;++peer)for(unsigned slot=0;slot<2;++slot){
        memset(entities,0,sizeof(entities));memset(&state,0,sizeof(state));memset(&peer_core,0,sizeof(peer_core));memset(&player_core,0,sizeof(player_core));
        entities[3].next=&entities[0];entities[0].next=&entities[1];entities[1].next=&entities[2];
        entities[0].core=&state;entities[0].parent=&entities[1];entities[2].core=peer?&peer_core:NULL;
        peer_core.field04.bytes.kind=peer==1?4:2;entities[3].core=&player_core;player_core.field04.bytes.kind=4;
        state.coreFlags=0xA5000003|phases[phase];state.hpAlive=health[alive];state.field04.bytes.kind=kinds[kind];
        for(unsigned i=0;i<4;++i){RenderObjectEntity *o=&entities[i].renderObject;
            o->header=&header;o->draw_count=1;o->flags_9C=0x8002;o->primitive_red=17;o->primitive_green=34;o->primitive_blue=51;}
        for(unsigned i=0;i<7;++i)g_BattleActionActors[i]=i<arrays[array][1]?&entities[i&1?1:2]:NULL;
        g_BattleActionActorCount=arrays[array][0];g_BattleFadeLevel=fades[fade];g_BattleActionIndex=0;
        g_BattleActionQueue[0].actor=&entities[1];g_BattleActionQueue[0].command=commands[command];g_ActiveDrawSlot=slot;
        action.actionCode.actionId=a?6:5;int result=Battle_ProcessActionSlot(&entities[0]);
        SLOT_PUT(result,4);SLOT_PUT(state.coreFlags,4);SLOT_PUT(g_BattleActionActorCount,1);SLOT_PUT(g_BattleFadeLevel,1);
        for(unsigned i=0;i<7;++i){unsigned id=0;for(unsigned j=0;j<4;++j)if(g_BattleActionActors[i]==&entities[j])id=j+1;
            if(g_BattleActionActors[i] && !id)valid=0;SLOT_PUT(id,1);}
        for(unsigned i=0;i<4;++i){RenderObjectEntity *o=&entities[i].renderObject;SLOT_PUT(o->flags_9C,2);
            SLOT_PUT(o->primitive_red,1);SLOT_PUT(o->primitive_green,1);SLOT_PUT(o->primitive_blue,1);}
        ++cases;
    }
    state.coreFlags=0x2000;g_BattleActionActorCount=7;g_BattleActionQueue[0].command=0;action.actionCode.actionId=5;
    for(unsigned i=0;i<7;++i)g_BattleActionActors[i]=&entities[1];
    Battle_ProcessActionSlot(&entities[0]);
    if(g_BattleActionActorCount!=7)valid=0;
    for(unsigned i=0;i<7;++i)if(g_BattleActionActors[i]!=&entities[1])valid=0;
#undef SLOT_PUT
    g_ActiveActor=saved_active;g_PlayerEntity=saved_player;g_FieldActorListHead=saved_head;
    memcpy(g_BattleActionActors,saved_actors,sizeof(saved_actors));g_BattleActionQueue[0]=saved_queue;
    g_BattleActionActorCount=saved_count;g_BattleFadeLevel=saved_fade;g_BattleActionIndex=saved_index;g_ActiveDrawSlot=saved_slot;
    if(!valid || hash!=UINT64_C(0x049bef90c93d53e1)){
        fprintf(stderr,"Original action slot: cases=%u hash=%016llx valid=%d\n",cases,(unsigned long long)hash,valid);return 0;}
    puts("Original action slot: 129600 USA cases, seven-actor list clear/append, commands, all phases, kinds/HP/fades, parent/peer/player exclusion and both buffers match");return 1;
}

static int original_battle_target_list(void)
{
    BattleTarget saved_targets[BATTLE_TARGET_CAPACITY+1];memcpy(saved_targets,g_BattleTargetList,sizeof(saved_targets));
    BattleSavedPosition saved_position=g_AyaSavedPos;
    s8 saved_index=g_BattleTargetIndex;
    FieldActor *saved_head=g_FieldActorListHead,*saved_player=g_PlayerEntity;
    BattleEntity player={0},entities[9]={{0}};EnemyCombatant cores[9]={{0}};
    const u32 flag_patterns[]={0,0x40,0x2040,0x4000,0x4040,0x2000,0x80,UINT32_MAX};
    const unsigned missing_masks[]={0,0x55,0x7F};
    const s16 players[][2]={{0,0},{-32768,32767},{100,-100}};
    const s16 points[][7][2]={
        {{100,100},{100,100},{100,100},{100,100},{100,100},{100,100},{100,100}},
        {{700,50},{600,-50},{500,50},{400,-50},{300,50},{200,-50},{100,50}},
        {{-32768,32767},{32767,-32768},{-32768,-32768},{32767,32767},{0,32767},{-32768,0},{0,0}},
        {{100,0},{0,100},{-100,0},{0,-100},{70,70},{-70,70},{70,-70}}};
    g_PlayerEntity=(FieldActor *)&player;
    uint64_t hash=UINT64_C(14695981039346656037);int valid=1;unsigned cases=0,histogram[8]={0};
#define TARGET_PUT(value,bytes) do {uint64_t v_=(uint64_t)(value); \
    for(unsigned b_=0;b_<(bytes);++b_){hash=(hash^(u8)v_)*UINT64_C(1099511628211);v_>>=8;}}while(0)
    for(unsigned mask=0;mask<128;++mask)for(unsigned pattern=0;pattern<9;++pattern)
    for(unsigned missing=0;missing<3;++missing)for(unsigned layout=0;layout<4;++layout)
    for(unsigned origin=0;origin<3;++origin)for(unsigned insert=0;insert<2;++insert){
        memset(g_BattleTargetList,0xA5,sizeof(g_BattleTargetList));
        g_AyaSavedPos=(BattleSavedPosition){0x11223344,0x55667788,(s32)UINT32_C(0x99AABBCC)};
        player.posX.parts.integer=players[origin][0];player.posZ.parts.integer=players[origin][1];
        BattleEntity *sequence[8];unsigned candidate=0;
        for(unsigned i=0;i<8;++i)sequence[i]=i==(insert?3:0)?&player:&entities[candidate++];
        for(unsigned i=0;i<8;++i)sequence[i]->next=i<7?sequence[i+1]:NULL;
        g_FieldActorListHead=(FieldActor *)sequence[0];
        for(unsigned i=0;i<7;++i){
            entities[i].core=missing_masks[missing]>>i&1?NULL:&cores[i];
            cores[i].hpAlive=mask>>i&1?(int)i*10+1:(i&1?-1:0);
            entities[i].entityFlags=pattern==8?0:flag_patterns[(i+pattern)%8];
            entities[i].renderObject.target_x=points[layout][i][0];entities[i].renderObject.target_z=points[layout][i][1];
        }
        int returned=Battle_BuildTargetList();unsigned n=(u8)g_BattleTargetIndex;
        if(returned!=(int)n)valid=0;
        if(n>7){valid=0;goto targets_restore;}++histogram[n];TARGET_PUT(n,1);
        for(unsigned i=0;i<n;++i){
            unsigned id=0;for(unsigned j=0;j<7;++j)if(g_BattleTargetList[i].actor==&entities[j])id=j+1;
            if(!id)valid=0;TARGET_PUT(id,1);TARGET_PUT(g_BattleTargetList[i].dist,4);TARGET_PUT(g_BattleTargetList[i].angle,2);
            TARGET_PUT(g_BattleTargetList[i].pad_0A[0],1);TARGET_PUT(g_BattleTargetList[i].pad_0A[1],1);
        }
        TARGET_PUT(g_BattleTargetList[n].actor==NULL,1);
        TARGET_PUT(g_AyaSavedPos.x,4);TARGET_PUT(g_AyaSavedPos.y,4);TARGET_PUT(g_AyaSavedPos.z,4);++cases;
    }
    /* Invalid population cannot overwrite the saved pose or the sentinel. */
    for(unsigned i=0;i<9;++i){entities[i].core=&cores[i];cores[i].hpAlive=1;entities[i].entityFlags=0;entities[i].next=i<8?&entities[i+1]:NULL;}
    g_FieldActorListHead=(FieldActor *)entities;
    g_AyaSavedPos=(BattleSavedPosition){1,2,3};Battle_BuildTargetList();
    if(g_BattleTargetIndex!=7 || g_BattleTargetList[7].actor || g_AyaSavedPos.x || g_AyaSavedPos.y!=2 || g_AyaSavedPos.z!=3)valid=0;
    g_PlayerEntity=NULL;Battle_BuildTargetList();if(g_BattleTargetIndex || g_BattleTargetList[0].actor)valid=0;
targets_restore:
#undef TARGET_PUT
    memcpy(g_BattleTargetList,saved_targets,sizeof(saved_targets));g_AyaSavedPos=saved_position;
    g_BattleTargetIndex=saved_index;g_FieldActorListHead=saved_head;g_PlayerEntity=saved_player;
    if(!valid || hash!=UINT64_C(0xcd7e48e9d9318f85)){
        fprintf(stderr,"Original battle targets: cases=%u hash=%016llx valid=%d\n",cases,(unsigned long long)hash,valid);return 0;}
    puts("Original battle targets: 82944 unhooked USA cases, filtering, null/dead cores, distances, angles, ordering/ties, full seven-target sentinel and saved-X alias match");return 1;
}

static unsigned fx_node_id(FxCommonNode *node)
{
    if(!node)return 0;if(node==&g_FxCommonDefaultNode)return 1;
    for(unsigned i=0;i<3;++i){if(node==&g_FxCommonListHeadNodes[i])return 202+i;
        if(node==&g_FxCommonListTailNodes[i])return 205+i;}
    for(unsigned i=0;i<200;++i)if(node==&g_FxCommonRecords[i])return i+2;
    return 0xffff;
}

static int original_gte_register_transfers(void)
{
    const u32 values[]={0,1,0x7fff,0x8000,0xffff,0x10000,0x7fffffff,0x80000000,0xffffffff,0x12345678};
    uint64_t hash=UINT64_C(14695981039346656037);unsigned cases=0;
    for(unsigned f=0;f<15;++f)for(unsigned v=0;v<10;++v)for(unsigned seed=0;seed<3;++seed){
        u32 value=values[v],buffer[12];
        for(unsigned j=0;j<32;++j){Psyz_GteDataWrite(j,value^(j*0x01010101u)^(seed*0x03456789u));
            Psyz_GteCtrlWrite(j,value+j*0x10203u+seed*0x07654321u);}
        for(unsigned j=0;j<12;++j)buffer[j]=value^(j*0x01020304u)^(seed*0x00112233u);
        s32 a=(s32)value,b=(s32)(value^0x80000000u),c=(s32)~value,d=(s32)(seed*0xfedcba98u);
        switch(f){
        case 0:SetVertex0(buffer);break;case 1:SetVertex1(buffer);break;case 2:SetVertex2(buffer);break;
        case 3:SetVertexTri(buffer,buffer+4,buffer+8);break;case 4:SetRGBfifo(buffer,buffer+4,buffer+8);break;
        case 5:SetIR123(a,b,c);break;case 6:SetIR0(a);break;case 7:SetSZfifo3(a,b,c);break;
        case 8:SetSZfifo4(a,b,c,d);break;case 9:SetSXSYfifo(a,b,c);break;case 10:SetRii(a,b,c);break;
        case 11:SetMAC123(a,b,c);break;case 12:SetData32(a);break;case 13:SetDQA(a);break;case 14:SetDQB(a);break;
        }
        for(unsigned bank=0;bank<2;++bank)for(unsigned j=0;j<32;++j){
            u32 word=bank?Psyz_GteCtrlRead(j):Psyz_GteDataRead(j);
            for(unsigned byte=0;byte<4;++byte)hash=(hash^(u8)(word>>(byte*8)))*UINT64_C(1099511628211);
        }
        ++cases;
    }
    /* Original short-vector averages accept packed vectors within particle
     * records. Exercise every byte alignment, signed Z's full-word store,
     * output guards and both supported in-place destinations. */
    const s16 average_first[]={-1200,600,-300,0};
    const s16 average_second[]={800,-200,-500,0};
    for(unsigned shift=0;shift<2;++shift)for(unsigned a=0;a<4;++a)
    for(unsigned b=0;b<4;++b)for(unsigned o=0;o<4;++o){
        _Alignas(u32) u8 first[16],second[16],output[16];
        const s16 expected[]={shift?300:-400,shift?0:400,shift?-450:-800,-1};
        void (*average)(void *,void *,int,int,void * volatile)=shift?LoadAverageShort12:LoadAverageShort0;
        memset(first,0xa5,sizeof(first));memset(second,0xa5,sizeof(second));memset(output,0xa5,sizeof(output));
        memcpy(first+a,average_first,8);memcpy(second+b,average_second,8);
        average(first+a,second+b,shift?1024:1,shift?3072:1,output+o);
        if(memcmp(output+o,expected,8))return 0;
        for(unsigned i=0;i<16;++i)if((i<o || i>=o+8) && output[i]!=0xa5)return 0;
        average(first+a,second+b,shift?1024:1,shift?3072:1,first+a);
        if(memcmp(first+a,expected,8))return 0;
        memcpy(first+a,average_first,8);
        average(first+a,second+b,shift?1024:1,shift?3072:1,second+b);
        if(memcmp(second+b,expected,8))return 0;
    }
    puts("Original GTE short averages: 128 alignment combinations, signed XYZ/padding, guards and both in-place outputs passed");
    /* Leave the GTE in a clean state before checking display initialization. */
    for(unsigned i=0;i<32;++i){Psyz_GteDataWrite(i,0);Psyz_GteCtrlWrite(i,0);}
    if(cases!=450 || hash!=UINT64_C(0x3bcbb387746aa626)){
        fprintf(stderr,"Original GTE REG03: cases=%u hash=%016llx\n",cases,(unsigned long long)hash);return 0;}
    puts("Original GTE REG03: all 15 original entry points match USA CPU/COP2 transfers across 450 scenarios and all 64 readable data/control registers");return 1;
}

static int original_fx_fog(void)
{
    const s32 near_values[]={-32768,-1000,-1,0,1,100,1000,6500,32767,65535};
    const s32 far_values[]={-1000,-1,0,100,199,200,1000,6500,11500,32767,65535};
    const s32 distances[]={-32768,-1,1,160,320,768,32767};
    uint64_t hash=UINT64_C(14695981039346656037);unsigned cases=0;
    for(unsigned a=0;a<10;++a)for(unsigned b=0;b<11;++b)for(unsigned c=0;c<7;++c){
        SetDQA(0x1234);SetDQB(0x12345678);
        SetFogNearFar(near_values[a],far_values[b],distances[c]);
        const u32 values[]={Psyz_GteCtrlRead(27),Psyz_GteCtrlRead(28)};
        for(unsigned i=0;i<2;++i)for(unsigned byte=0;byte<4;++byte)
            hash=(hash^(u8)(values[i]>>(byte*8)))*UINT64_C(1099511628211);
        ++cases;
    }
    SetDQA(0x1234);SetDQB(0x12345678);SetFogNearFar(6500,11500,0);
    if(cases!=770 || hash!=UINT64_C(0x2b60b43c2716c8c8) ||
       Psyz_GteCtrlRead(27)!=0x1234 || Psyz_GteCtrlRead(28)!=0x12345678){
        fprintf(stderr,"Original FX fog: cases=%u hash=%016llx\n",cases,(unsigned long long)hash);return 0;}
    puts("Original FX fog: 770 USA CPU/GTE cases match DQA/DQB, range rejection, signed scales and clamps; zero projection guard preserves registers");return 1;
}

DISPENV *GetDispEnv(DISPENV *env);
static int original_fx_display(void)
{
    Pe1GameState saved=g_GameState;FxCommonFrame frames[2];
    FxCommonBuffer *active=g_FxCommonActiveBuffer;
    memcpy(frames,g_FxCommonFrames,sizeof(frames));
    Boot_InitMemoryLayout();
    int arena_fits=g_RenderScratchBufferBase[0]+2*sizeof(FxCommonBufferRegion)<=g_RenderFrontBufferBase[0];
    static struct {u64 before;RenderNativeTag entries[4096];u64 after;} ordering[2];
    static struct {u64 before;FxCommonBufferRegion regions[2];u64 after;} packets;
    static u16 old_pixels[1024*512],pixels[1024*512];
    DRAWENV old_draw;DISPENV old_display;RECT area={0,0,1024,512};
    GetDrawEnv(&old_draw);GetDispEnv(&old_display);StoreImage(&area,old_pixels);DrawSync(0);
    for(unsigned i=0;i<1024*512;++i)pixels[i]=0x1357;
    LoadImage(&area,pixels);DrawSync(0);
    memset(ordering,0xa5,sizeof(ordering));memset(&packets,0xa5,sizeof(packets));
    memset(g_FxCommonFrames,0xa5,sizeof(g_FxCommonFrames));
    g_OtBufferTable[0]=(u8 *)ordering[0].entries;g_RenderOtBufferBaseAlt[0]=(u8 *)ordering[1].entries;
    g_RenderScratchBufferBase[0]=(u8 *)packets.regions;
    RenderNativeTag terminal;ClearOTagR(&terminal,1);
    FxCommon_InitDisplay();int valid=arena_fits && g_FxCommonActiveBuffer==&g_FxCommonFrames[0].buffer;
    for(unsigned i=0;i<2;++i){
        FxCommonFrame *frame=&g_FxCommonFrames[i];
        if(frame->buffer.data!=(u8 *)&packets.regions[i] ||
           frame->buffer.allocation!=(void *)ordering[i].entries ||
           frame->drawEnv.clip.x || frame->drawEnv.clip.y!=i*240 ||
           frame->drawEnv.clip.w!=320 || frame->drawEnv.clip.h!=240 ||
           frame->drawEnv.ofs[0] || frame->drawEnv.ofs[1]!=i*240 ||
           frame->drawEnv.isbg || frame->drawEnv.r0 || frame->drawEnv.g0 || frame->drawEnv.b0 ||
           frame->dispEnv.disp.x || frame->dispEnv.disp.y!=(1-i)*240 ||
           frame->dispEnv.disp.w!=320 || frame->dispEnv.disp.h!=240 || frame->dispEnv.isrgb24)valid=0;
        for(unsigned j=0;j<4096;++j)if(ordering[i].entries[j].length ||
            ordering[i].entries[j].address!=(j?(uintptr_t)&ordering[i].entries[j-1]:terminal.address))valid=0;
        if(ordering[i].before!=UINT64_C(0xa5a5a5a5a5a5a5a5) || ordering[i].after!=UINT64_C(0xa5a5a5a5a5a5a5a5))valid=0;
    }
    if(Psyz_GteCtrlRead(24)!=(160u<<16) || Psyz_GteCtrlRead(25)!=(120u<<16) || Psyz_GteCtrlRead(26)!=768)valid=0;
    StoreImage(&area,pixels);DrawSync(0);
    for(unsigned y=0;y<512;++y)for(unsigned x=0;x<1024;++x)
        if(pixels[y*1024+x]!=(x<320 && y<480?0:0x1357))valid=0;
    for(unsigned i=0;i<2;++i){
        memset(ordering[i].entries,0xa5,sizeof(ordering[i].entries));
        g_FxCommonActiveBuffer=&g_FxCommonFrames[i].buffer;
        g_FxCommonActiveBuffer->data+=sizeof(FxCommonBufferRegion)-16;
        FxCommon_ClearActiveOrdering();
        if(g_FxCommonActiveBuffer->data!=(u8 *)&packets.regions[i])valid=0;
        for(unsigned j=0;j<4096;++j)if(ordering[i].entries[j].length ||
            ordering[i].entries[j].address!=(j?(uintptr_t)&ordering[i].entries[j-1]:terminal.address))valid=0;
    }
    for(unsigned i=0;i<sizeof(packets);++i)if(((u8 *)&packets)[i]!=0xa5)valid=0;
    LoadImage(&area,old_pixels);PutDrawEnv(&old_draw);PutDispEnv(&old_display);DrawSync(0);
    g_GameState=saved;memcpy(g_FxCommonFrames,frames,sizeof(frames));g_FxCommonActiveBuffer=active;
    if(!valid){fprintf(stderr,"Original FX display: frame/env/OT/VRAM regression\n");return 0;}
    puts("Original FX display: typed double buffers, native 4096-entry OTs, complete VRAM clear footprint, GTE setup, both arena resets and guards pass headlessly");return 1;
}

static int original_fx_node_pool(void)
{
    const unsigned counts[]={0,1,2,7,63,199,200};
    uint64_t hash=UINT64_C(14695981039346656037);unsigned cases=0;int valid=1;
    _Alignas(16) u8 table[1024]={0};
    for(unsigned i=0;i<8;++i){u32 offset=32+i*64,value=100+i*7;memcpy(table+i*4,&offset,4);memcpy(table+offset+48,&value,4);}
#define FX_PUT(value,bytes) do {uint64_t v_=(uint64_t)(value); \
    for(unsigned b_=0;b_<(bytes);++b_){hash=(hash^(u8)v_)*UINT64_C(1099511628211);v_>>=8;}}while(0)
    for(unsigned count=0;count<7;++count)for(unsigned pattern=0;pattern<5;++pattern)for(unsigned offset=0;offset<3;++offset){
        memset(&g_FxCommonRuntime,0,sizeof(g_FxCommonRuntime));memset(&g_FxCommonDefaultNode,0,sizeof(g_FxCommonDefaultNode));
        memset(g_FxCommonListHeadNodes,0,3*sizeof(FxCommonNode));memset(g_FxCommonListTailNodes,0,3*sizeof(FxCommonNode));
        memset(g_FxCommonIndexFreeNext,0,200*sizeof(s16));g_FxCommonIndexFreeHead=g_FxCommonIndexLast=g_FxCommonRecordCursor=0;
        FxCommon_InitNodePools();FxCommonNode *live[200];unsigned kinds[200]={0},n=0,removed=0;
        for(unsigned stage=0;stage<2;++stage){
            unsigned number=stage?removed/2:counts[count];
            for(unsigned i=0;i<number;++i){unsigned serial=stage?counts[count]+i:i,kind=(serial+offset)%3;
                FxCommonNode *parent=&g_FxCommonDefaultNode;if(serial%4==3 && n && live[0]->bucket==1)parent=live[0];
                FxCommonNode *node=kind==0?FxCommon_CreateBareNode(parent,table):
                    kind==1?FxCommon_CreateTypedNode(parent,serial%8,table):
                    FxCommon_CreateConfiguredNode(parent,serial%8,(serial+1)%8,(serial+2)%8,1000+serial,500+serial,table);
                if(!node || n>=200 || (unsigned)node->id>=200){valid=0;break;}
                kinds[node->id]=kind;live[n++]=node;
            }
            if(!stage){unsigned initial=n;FxCommonNode *original[200];memcpy(original,live,n*sizeof(*live));
                for(unsigned i=0;i<initial;++i){FxCommonNode *node=original[i];unsigned id=node->id;
                    int release=pattern==4 || (pattern==1 && !(id%2)) || (pattern==2 && id%2) || (pattern==3 && !(id%3));
                    for(unsigned j=0;j<n;++j)if(live[j]!=node && live[j]->parent==node)release=0;
                    if(release){FxCommon_ReleaseNode(node);unsigned j=0;while(j<n && live[j]!=node)++j;
                        memmove(live+j,live+j+1,(n-j-1)*sizeof(*live));--n;++removed;}
                }
            }
        }
        FX_PUT(g_FxCommonIndexFreeHead,2);FX_PUT(g_FxCommonIndexLast,2);
        for(unsigned i=0;i<200;++i)FX_PUT(g_FxCommonIndexFreeNext[i],2);
        for(unsigned i=0;i<202;++i){FX_PUT(g_FxCommonIndexLinks[i].previous,2);FX_PUT(g_FxCommonIndexLinks[i].next,2);}
        FxCommonNode *ordered[200],*node;unsigned length=0;FxCommon_BeginRecordIteration();
        while((node=FxCommon_NextRecord())){if(length>=200){valid=0;break;}ordered[length++]=node;}
        FX_PUT(length,2);if(length!=n)valid=0;
        for(unsigned i=0;i<length;++i){node=ordered[i];unsigned kind=kinds[node->id];
            FX_PUT(node->id,2);FX_PUT(kind,1);FX_PUT(node->type,2);FX_PUT(node->resource==table,1);
            if(kind==2){FX_PUT(node->middleKind,2);FX_PUT(node->nearKind,2);FX_PUT(node->lod.margin,2);FX_PUT(node->lod.farDepth,4);FX_PUT(node->lod.nearDepth,4);}
            else if(kind==1)FX_PUT(node->margin,2);
            for(unsigned j=0;j<3;++j)FX_PUT(kind==2?node->lod.matrix.t[j]:node->matrix.t[j],4);
            FX_PUT(node->bucket,2);FX_PUT(fx_node_id(node->parent),2);FX_PUT(fx_node_id(node->previous),2);FX_PUT(fx_node_id(node->next),2);
        }
        for(unsigned i=0;i<3;++i){FX_PUT(fx_node_id(g_FxCommonListHeadNodes[i].next),2);FX_PUT(fx_node_id(g_FxCommonListTailNodes[i].previous),2);}
        ++cases;
    }
#undef FX_PUT
    if(cases!=105 || hash!=UINT64_C(0x405afb1f82d27f95))valid=0;
    /* Exhaustion and malformed ownership cannot consume a slot or a sentinel. */
    FxCommon_InitNodePools();for(unsigned i=0;i<200;++i)if(!FxCommon_CreateBareNode(&g_FxCommonDefaultNode,NULL))valid=0;
    FxCommonRuntime full=g_FxCommonRuntime;
    if(FxCommon_AllocIndexLink()!=-1 || FxCommon_CreateBareNode(&g_FxCommonDefaultNode,NULL) || memcmp(&full,&g_FxCommonRuntime,sizeof(full)))valid=0;
    FxCommon_ReleaseNode(&g_FxCommonRecords[199]);FxCommonRuntime freed=g_FxCommonRuntime;
    FxCommon_ReleaseNode(&g_FxCommonRecords[199]);FxCommon_FreeIndexLink(-1);FxCommon_FreeIndexLink(200);
    if(memcmp(&freed,&g_FxCommonRuntime,sizeof(freed)))valid=0;
    FxCommonNode bad={.bucket=2};u16 head=g_FxCommonIndexFreeHead;
    if(FxCommon_CreateBareNode(NULL,NULL) || FxCommon_CreateBareNode(&bad,NULL) ||
       FxCommon_CreateTypedNode(&g_FxCommonDefaultNode,0,NULL) ||
       FxCommon_CreateConfiguredNode(&g_FxCommonDefaultNode,0,0,0,1,2,NULL) || g_FxCommonIndexFreeHead!=head)valid=0;
    FxCommon_InitNodePools();FxCommon_BeginRecordIteration();for(unsigned i=0;i<4;++i)if(FxCommon_NextRecord())valid=0;
    if(!valid){fprintf(stderr,"Original FX node pool: cases=%u hash=%016llx\n",cases,(unsigned long long)hash);return 0;}
    puts("Original FX node pool: 105 unhooked USA scenarios, all 200 slots, three constructors, parent/bucket links, frees/reuse and record iteration match; native exhaustion/double-free guards pass");return 1;
}

static int original_fx_culling(void)
{
    const s32 scales[]={1,8,4096,32767},depths[]={0,1,-1,INT32_MAX,INT32_MIN};
    const s32 points[][3]={{0,0,0},{1,-1,1},{-1,1,-1},{512,-512,500},
        {32767,-32768,32760},{-32768,32767,-32760},{INT32_MAX,INT32_MIN,1},
        {INT32_MIN,INT32_MAX,-1},{1234,-5678,9012}};
    const s32 sides[][4]={{1,1,1,1},{-1,-1,-1,-1},{1,-1,1,-1},{0,0,0,0},{0,1,-1,0}};
    const s32 margins[]={-32768,-1,0,1,12,4095,0x10000000,INT32_MAX},lengths[]={1,64,65535};
    GteVector *normals[]={&g_FxCommonFrustumNormal0,&g_FxCommonFrustumNormal1,&g_FxCommonFrustumNormal2,&g_FxCommonFrustumNormal3};
    s32 *distance[]={&g_FxCommonFrustumDistance0,&g_FxCommonFrustumDistance1,&g_FxCommonFrustumDistance2,&g_FxCommonFrustumDistance3};
    s32 *side[]={&g_FxCommonFrustumSide0,&g_FxCommonFrustumSide1,&g_FxCommonFrustumSide2,&g_FxCommonFrustumSide3};
    s32 *length[]={&g_FxCommonFrustumLength0,&g_FxCommonFrustumLength1,&g_FxCommonFrustumLength2,&g_FxCommonFrustumLength3};
    uint64_t hash=UINT64_C(14695981039346656037);unsigned cases=0;
    for(unsigned a=0;a<4;++a)for(unsigned b=0;b<9;++b)for(unsigned c=0;c<5;++c)
    for(unsigned d=0;d<5;++d)for(unsigned e=0;e<8;++e)for(unsigned f=0;f<3;++f){
        s32 scale=scales[a],n[][3]={{scale,1,-scale},{-scale,scale,1},{1,-scale,scale},{scale,-scale,scale}};
        s32 point[3];s16 shortpoint[3];
        for(unsigned i=0;i<4;++i){normals[i]->x=n[i][0];normals[i]->y=n[i][1];normals[i]->z=n[i][2];
            *distance[i]=(s32)((u32)depths[c]+i*17u);*side[i]=sides[d][i];*length[i]=lengths[f]+i;}
        for(unsigned i=0;i<3;++i){point[i]=points[b][i];shortpoint[i]=(s16)point[i];}
        s32 result[]={FxCommon_IsPointInView(point),FxCommon_IsShortPointInView(shortpoint),
            FxCommon_CheckBoundsWithMargin(point,margins[e]),FxCommon_CheckFourBoundsWithMargin(point,margins[e])};
        for(unsigned i=0;i<4;++i)for(unsigned byte=0;byte<4;++byte)
            hash=(hash^(u8)((u32)result[i]>>(byte*8)))*UINT64_C(1099511628211);
        ++cases;
    }
    int valid=cases==21600 && hash==UINT64_C(0xa642cfccbdbba705);
    s32 point[3]={0};
    for(unsigned i=0;i<4;++i){*length[i]=0;
        if(FxCommon_CheckFourBoundsWithMargin(point,1))valid=0;*length[i]=1;}
    g_FxCommonFrustumLength0=0;
    if(FxCommon_CheckBoundsWithMargin(point,1) || FxCommon_IsPointInView(NULL) ||
       FxCommon_IsShortPointInView(NULL) || FxCommon_CheckBoundsWithMargin(NULL,1) ||
       FxCommon_CheckFourBoundsWithMargin(NULL,1))valid=0;
    if(!valid){fprintf(stderr,"Original FX culling: cases=%u hash=%016llx\n",cases,(unsigned long long)hash);return 0;}
    puts("Original FX culling: 21600 unhooked USA scenarios match all four checks, including signed boundaries and 32-bit overflow; null/zero-length guards pass");return 1;
}

static int original_fx_camera_matrices(void)
{
    const s32 poses[][2][3]={{{0,0,0},{0,0,1000}},{{100,-200,300},{-300,500,2000}},
        {{-1000,2000,-3000},{4000,-5000,6000}},{{0,0,0},{0,1000,0}},
        {{0,0,0},{0,-1000,0}},{{1,1,1},{1,1,1}}};
    GteVector *normals[]={&g_FxCommonFrustumNormal0,&g_FxCommonFrustumNormal1,&g_FxCommonFrustumNormal2,&g_FxCommonFrustumNormal3};
    s32 *distance[]={&g_FxCommonFrustumDistance0,&g_FxCommonFrustumDistance1,&g_FxCommonFrustumDistance2,&g_FxCommonFrustumDistance3};
    s32 *side[]={&g_FxCommonFrustumSide0,&g_FxCommonFrustumSide1,&g_FxCommonFrustumSide2,&g_FxCommonFrustumSide3};
    s32 *length[]={&g_FxCommonFrustumLength0,&g_FxCommonFrustumLength1,&g_FxCommonFrustumLength2,&g_FxCommonFrustumLength3};
    uint64_t hash=UINT64_C(14695981039346656037);
#define FX_HASH_BYTES(ptr,count) do {const u8 *bytes_=(const u8 *)(ptr); \
    for(unsigned byte_=0;byte_<(count);++byte_)hash=(hash^bytes_[byte_])*UINT64_C(1099511628211);}while(0)
    for(unsigned i=0;i<6;++i){
        for(unsigned j=0;j<32;++j){Psyz_GteDataWrite(j,0);Psyz_GteCtrlWrite(j,0);}
        memcpy(&g_FxCommonCameraPosition,poses[i][0],12);memcpy(&g_FxCommonCameraTarget,poses[i][1],12);
        memset(g_FxCommonCameraAngles,0,sizeof(GteShortVector));
        FxCommon_UpdateCameraMatrices();FxCommon_UpdateFrustumPlanes(&g_FxCommonCameraPosition);
        /* MATRIX has two implicit alignment bytes, which carry no game state. */
        FX_HASH_BYTES(g_FxCommonViewMatrix.m,18);FX_HASH_BYTES(g_FxCommonViewMatrix.t,12);
        FX_HASH_BYTES(g_FxCommonCameraMatrix.m,18);FX_HASH_BYTES(g_FxCommonCameraMatrix.t,12);
        FX_HASH_BYTES(g_FxCommonCameraAngles,8);
        for(unsigned j=0;j<4;++j){FX_HASH_BYTES(normals[j],12);FX_HASH_BYTES(distance[j],4);
            FX_HASH_BYTES(side[j],4);FX_HASH_BYTES(length[j],4);}
    }
#undef FX_HASH_BYTES
    if(hash!=UINT64_C(0xab55eca183324865)){
        fprintf(stderr,"Original FX camera matrices: hash=%016llx\n",(unsigned long long)hash);return 0;}
    puts("Original FX camera: six USA CPU/GTE scenarios match both matrices, angles and all four frustum planes, including vertical and coincident positions");return 1;
}

static int original_fx_poly_render_assets(PeDisc *disc)
{
    PeDiscFile archive;if(!pe_disc_find(disc,"PE.IMG",&archive))return 0;
    static _Alignas(16) u8 bank[100*2048],synthetic[2048];
    static struct {u64 before;_Alignas(16) u8 bytes[0x40000];u64 after;} arena;
    static struct {u64 before;RenderNativeTag entries[4096];u64 after;} ot;
    const s32 z_values[]={-1000,1,768,7980,7990,8000,8016,8144,8160,8176,11200,11216,11232,20000},biases[]={0,500,1000};
    const u8 codes[]={0x20,0x28,0x30,0x38,0x24,0x2c,0x34,0x3c};
    const unsigned strides[]={sizeof(FxCommonFlatTriangle),sizeof(FxCommonFlatQuad),sizeof(FxCommonColoredTriangle),sizeof(FxCommonColoredQuad),
        sizeof(FxCommonTexturedTriangle),sizeof(FxCommonTexturedQuad),sizeof(FxCommonColoredTexturedTriangle),sizeof(FxCommonColoredTexturedQuad)};
    FxCommonPolyResource *header=(FxCommonPolyResource *)synthetic;memset(synthetic,0,sizeof(synthetic));unsigned cursor=48;
    for(unsigned kind=0;kind<8;++kind){header->counts[kind]=2;header->offsets[kind]=cursor;
        for(unsigned reverse=0;reverse<2;++reverse){u8 *primitive=synthetic+cursor;unsigned quad=kind%2,colors=kind<2 || (kind>=4 && kind<6)?1:quad?4:3,at=colors*4;
            for(unsigned i=0;i<colors;++i){primitive[i*4]=31+i*47;primitive[i*4+1]=67+i*23;primitive[i*4+2]=101+i*31;primitive[i*4+3]=i?0xcd:codes[kind];}
            if(kind>=4){for(unsigned texture=0;texture<(kind<6?3u:1u);++texture){
                    const u16 uv[]={(11+texture*3)|((23+texture*5)<<8),0x7100+texture,(101+texture*2)|((203+texture*7)<<8),0x101+texture,(77+texture)|((99+texture*9)<<8),231|((17+texture*11)<<8)};
                    memcpy(primitive+at,uv,sizeof(uv));at+=sizeof(uv);}
                if(kind>=6){unsigned unknown=quad?16:12;memset(primitive+at,0x5a,unknown);at+=unknown;}}
            GteShortVector vertices[]={{-100,-80,0,0},{100,-80,0,0},{-100,80,0,0},{100,80,0,0}};
            if(reverse){GteShortVector swap=vertices[1];vertices[1]=vertices[2];vertices[2]=swap;}
            memcpy(primitive+at,vertices,(quad?4:3)*sizeof(*vertices));cursor+=strides[kind];
        }
    }
    FxCommonBuffer *old_buffer=g_FxCommonActiveBuffer; s32 old_base=g_FxCommonDepthBase;
    FxCommonRenderScratchpad old_work=g_FxCommonRenderWorkspace;
    uint64_t hash=UINT64_C(14695981039346656037);unsigned cases=0,packets=0;int valid=1;
#define POLY_PUT(value,count) do {uint64_t word_=(uint64_t)(value); \
    for(unsigned byte_=0;byte_<(count);++byte_){hash=(hash^(u8)word_)*UINT64_C(1099511628211);word_>>=8;}}while(0)
    for(unsigned variant=0;variant<3 && valid;++variant){
        FxCommonPolyResource *resource=(FxCommonPolyResource *)synthetic;
        if(variant<2){unsigned sector=variant?2105:2016,size=variant?100:89;u32 offset;
            if(!pe_disc_read(disc,&archive,(uint64_t)sector*2048,bank,size*2048)){valid=0;break;}
            memcpy(&offset,bank+4,4);if(offset>size*2048-48){valid=0;break;}resource=(FxCommonPolyResource *)(bank+offset);}
        for(unsigned z=0;z<14 && valid;++z)for(unsigned bias=0;bias<3 && valid;++bias)for(unsigned mirror=0;mirror<2 && valid;++mirror){
            for(unsigned i=0;i<32;++i){Psyz_GteDataWrite(i,0);Psyz_GteCtrlWrite(i,0);}
            const u32 controls[]={4096,0,mirror?0xf000:4096,0,4096,0,0,(u32)z_values[z]};
            for(unsigned i=0;i<8;++i)Psyz_GteCtrlWrite(i,controls[i]);
            Psyz_GteCtrlWrite(24,160u<<16);Psyz_GteCtrlWrite(25,120u<<16);Psyz_GteCtrlWrite(26,768);Psyz_GteCtrlWrite(29,341);Psyz_GteCtrlWrite(30,256);
            memset(&g_FxCommonRenderWorkspace,0xa5,sizeof(g_FxCommonRenderWorkspace));memset(&arena,0xa5,sizeof(arena));memset(&ot,0xa5,sizeof(ot));
            for(unsigned i=0;i<4096;++i)ot.entries[i]=(RenderNativeTag){0xffffff,0xab};
            FxCommonBuffer buffer={.data=arena.bytes,.allocation=(RenderNativeTag *)ot.entries,.end=arena.bytes+sizeof(arena.bytes)};
            g_FxCommonActiveBuffer=&buffer;g_FxCommonDepthBase=biases[bias];FxCommon_DrawPolyResource(NULL,resource);
            if(buffer.error || buffer.data<arena.bytes || buffer.data>arena.bytes+sizeof(FxCommonBufferRegion)){fprintf(stderr,"FX poly resource=%u z=%d bias=%d mirror=%u error=%d used=%td\n",variant,z_values[z],biases[bias],mirror,buffer.error,buffer.data-arena.bytes);valid=0;break;}
            u8 *at=arena.bytes,*addresses[8192];unsigned count=0;
            while(at<buffer.data){RenderNativeTag *tag=(RenderNativeTag *)at;
                if(count>=8192 || (tag->length!=4 && tag->length!=5 && tag->length!=6 && tag->length!=7 && tag->length!=8 && tag->length!=9 && tag->length!=12)){valid=0;break;}
                addresses[count++]=at;size_t size=(sizeof(*tag)+tag->length*4+_Alignof(RenderNativeTag)-1)&~(size_t)(_Alignof(RenderNativeTag)-1);
                if(size>(size_t)(buffer.data-at)){valid=0;break;}at+=size;
            }
            if(!valid || at!=buffer.data){valid=0;break;}POLY_PUT(count,4);
            for(unsigned i=0;i<count;++i){RenderNativeTag *tag=(RenderNativeTag *)addresses[i];POLY_PUT(tag->length,1);
                for(unsigned byte=0;byte<tag->length*4;++byte)POLY_PUT(addresses[i][sizeof(*tag)+byte],1);}
            for(unsigned depth=0;depth<4096 && valid;++depth){if(ot.entries[depth].length!=0xab){valid=0;break;}
                uintptr_t link=ot.entries[depth].address;if(link==0xffffff)continue;POLY_PUT(depth,2);unsigned steps=0;
                while(link!=0xffffff){unsigned id=0;while(id<count && (uintptr_t)addresses[id]!=link)++id;
                    if(id==count || ++steps>count){valid=0;break;}POLY_PUT(id,2);link=((RenderNativeTag *)link)->address;}
                POLY_PUT(65535,2);
            }
            POLY_PUT(65535,2);packets+=count;++cases;
            if(arena.before!=UINT64_C(0xa5a5a5a5a5a5a5a5) || arena.after!=UINT64_C(0xa5a5a5a5a5a5a5a5) ||
               ot.before!=UINT64_C(0xa5a5a5a5a5a5a5a5) || ot.after!=UINT64_C(0xa5a5a5a5a5a5a5a5))valid=0;
            for(u8 *p=buffer.data;p<buffer.end;++p)if(*p!=0xa5){valid=0;break;}
        }
    }
#undef POLY_PUT
    const unsigned packet_sizes[]={sizeof(FxCommonF3Packet),sizeof(FxCommonF4Packet),sizeof(FxCommonG3Packet),sizeof(FxCommonG4Packet),
        sizeof(FxCommonFt3Packet),sizeof(FxCommonFt4Packet),sizeof(FxCommonGt3Packet),sizeof(FxCommonGt4Packet)};
    unsigned guards=0;
    for(unsigned kind=0;kind<8;++kind)for(unsigned fault=0;fault<4;++fault){
        /* Both single packets and all four subdivision packets must fit. */
        if(fault==1 && kind!=4 && kind!=5)continue;
        _Alignas(16) u8 minimal[128]={0};FxCommonPolyResource *one=(FxCommonPolyResource *)minimal;
        one->counts[kind]=1;for(unsigned i=0;i<8;++i)one->offsets[i]=48;
        memcpy(minimal+48,synthetic+header->offsets[kind],strides[kind]);
        for(unsigned i=0;i<32;++i){Psyz_GteDataWrite(i,0);Psyz_GteCtrlWrite(i,0);}
        const u32 controls[]={4096,0,4096,0,4096,0,0,fault==1?20000:1000};
        for(unsigned i=0;i<8;++i)Psyz_GteCtrlWrite(i,controls[i]);
        Psyz_GteCtrlWrite(26,768);Psyz_GteCtrlWrite(29,341);Psyz_GteCtrlWrite(30,256);
        memset(&g_FxCommonRenderWorkspace,0xa5,sizeof(g_FxCommonRenderWorkspace));memset(&arena,0xa5,sizeof(arena));memset(&ot,0xa5,sizeof(ot));
        FxCommonBuffer buffer={.data=arena.bytes,.allocation=(RenderNativeTag *)ot.entries,.end=arena.bytes+sizeof(arena.bytes)};
        if(fault<2)buffer.end=arena.bytes+packet_sizes[kind]*((kind==4 || kind==5) && !fault?4:1)-1;
        g_FxCommonActiveBuffer=&buffer;g_FxCommonDepthBase=fault==2?-1000:fault==3?100000:0;
        FxCommon_DrawPolyResource(NULL,one);
        int expected=fault<2?-1:kind<6?-2:0;
        if(buffer.error!=expected || buffer.data!=arena.bytes)valid=0;
        for(unsigned i=0;i<sizeof(arena);++i)if(((u8 *)&arena)[i]!=0xa5)valid=0;
        for(unsigned i=0;i<sizeof(ot);++i)if(((u8 *)&ot)[i]!=0xa5)valid=0;
        ++guards;
    }
    if(guards!=26)valid=0;
    g_FxCommonActiveBuffer=old_buffer;g_FxCommonDepthBase=old_base;g_FxCommonRenderWorkspace=old_work;
    if(!valid || cases!=252 || packets!=6322 || hash!=UINT64_C(0x8389f407efdeab28)){fprintf(stderr,"Original FX polygon resources: cases=%u packets=%u hash=%016llx valid=%d\n",cases,packets,(unsigned long long)hash,valid);return 0;}
    puts("Original FX polygon resources: both real banks and all eight synthetic streams match USA CPU/GTE across subdivision and texture-distance boundaries; native payloads and complete OT chains pass");return 1;
}

static int original_fx_shader_packets(void)
{
    FxCommonGradientQuad *quads[]={g_FxCommonSkyQuads,g_FxCommonShadeQuads,g_FxCommonFloorFadeQuads,g_FxCommonTopFadeQuads};
    FxCommonDrawModePacket *modes[]={g_FxCommonDrawModes0,g_FxCommonDrawModes1,g_FxCommonDrawModes2};
    for(unsigned i=0;i<4;++i)memset(quads[i],0xa5,2*sizeof(*quads[i]));
    for(unsigned i=0;i<3;++i)memset(modes[i],0xa5,2*sizeof(*modes[i]));
    FxCommon_InitFadeQuads();uint64_t hash=UINT64_C(14695981039346656037);int valid=1;
#define SHADER_PUT(value,count) do {uint64_t word_=(uint64_t)(value); \
    for(unsigned byte_=0;byte_<(count);++byte_){hash=(hash^(u8)word_)*UINT64_C(1099511628211);word_>>=8;}}while(0)
    for(unsigned group=0;group<7;++group)for(unsigned slot=0;slot<2;++slot){
        RenderNativeTag *tag=group<4?&quads[group][slot].tag:&modes[group-4][slot].tag;
        if(tag->length!=(group<4?8:1))valid=0;SHADER_PUT(tag->length,1);
        for(unsigned byte=0;byte<tag->length*4;++byte)SHADER_PUT(((u8 *)(tag+1))[byte],1);
        if(group>=4 && modes[group-4][slot].command>UINT32_MAX)valid=0;
    }
    if(hash!=UINT64_C(0x353e0cebf2681c3d))valid=0;
    static struct {u64 before;_Alignas(16) u8 data[4096];u64 after;} arena;
    static struct {u64 before;RenderNativeTag entries[4096];u64 after;} ot;
    s32 fades[605],full[262];for(unsigned i=0;i<601;++i)fades[i]=(s32)i-300;
    memcpy(fades+601,(s32[]){-32768,-32767,32766,32767},16);
    for(unsigned i=0;i<256;++i)full[i]=i;memcpy(full+256,(s32[]){-1,256,32767,65535,INT32_MAX,INT32_MIN},24);
    const s32 markers[]={-32768,-255,-254,-8,-1,0,1,8,247,254,255,32767},night[]={-16,0,16,255};
    const s32 *values[]={fades,full,markers,night};const unsigned numbers[]={605,262,12,4};
    unsigned cases=0,packets=0;
    for(unsigned kind=0;kind<4 && valid;++kind)for(unsigned v=0;v<numbers[kind] && valid;++v)
    for(unsigned slot=0;slot<2 && valid;++slot)for(unsigned pattern=0;pattern<(kind<2?1:4) && valid;++pattern)
    for(unsigned view=0;view<(kind<2?1:3) && valid;++view){
        for(unsigned i=0;i<32;++i){Psyz_GteDataWrite(i,0);Psyz_GteCtrlWrite(i,0);}
        const u32 controls[]={4096,0,4096,0,4096,0,0,(u32)(view==0?1000:view==1?8000:-1000)};
        for(unsigned i=0;i<8;++i)Psyz_GteCtrlWrite(i,controls[i]);
        Psyz_GteCtrlWrite(24,160u<<16);Psyz_GteCtrlWrite(25,120u<<16);Psyz_GteCtrlWrite(26,768);Psyz_GteCtrlWrite(29,341);Psyz_GteCtrlWrite(30,256);
        memset(&arena,0xa5,sizeof(arena));memset(&ot,0xa5,sizeof(ot));
        for(unsigned i=0;i<4096;++i)ot.entries[i]=(RenderNativeTag){0xffffff,0xab};
        FxCommonBuffer *buffer=&g_FxCommonFrames[slot].buffer;
        *buffer=(FxCommonBuffer){.data=arena.data,.allocation=ot.entries,.end=arena.data+sizeof(arena.data)};g_FxCommonActiveBuffer=buffer;
        for(unsigned i=0;i<10;++i){g_FxCommonMotionPositions[i].vector.x=(s32)(((u32)(i*37-150)&65535)<<16|i);
            g_FxCommonMotionPositions[i].vector.y=(s32)(((u32)(80-i*19)&65535)<<16|i*3);
            g_FxCommonMotionPositions[i].vector.z=(s32)(((u32)(i*131-300)&65535)<<16|i*7);g_FxCommonMotionPositions[i].pad0C=(s32)0xa5a5a5a5;
            memset(&g_FxCommonEffectSetup[i],0,sizeof(g_FxCommonEffectSetup[i]));
            g_FxCommonEffectSetup[i].position[0]=i*51-200;g_FxCommonEffectSetup[i].position[1]=i*29-100;g_FxCommonEffectSetup[i].position[2]=i*113-400;
            g_FxCommonEffectSetup[i].enabled=pattern==0?0:pattern==1?1:pattern==2?i%2:(i==0 || i==3 || i==9);}
        g_FxCommonRoomVariant=pattern*3;g_FxCommonMarkerFade=(s16)values[kind][v];g_FxCommonNight=pattern%2;s16 result=0;
        if(kind==0)result=FxCommon_StepFadeIntensity((s16)values[kind][v]);
        if(kind==1)FxCommon_DrawFullScreenFade(values[kind][v]);
        if(kind==2)result=FxCommon_DrawEffectMarkers((s16)values[kind][v]);
        if(kind==3){FxCommon_DrawNightShadeAndMarkers();result=g_FxCommonMarkerFade;}
        if(buffer->error || buffer->data<arena.data || buffer->data>buffer->end){valid=0;break;}
        u8 *cursor=arena.data,*addresses[64];unsigned count=0;
        while(cursor<buffer->data){RenderNativeTag *tag=(RenderNativeTag *)cursor;
            if(count>=64 || (tag->length!=1 && tag->length!=3 && tag->length!=8 && tag->length!=12)){valid=0;break;}
            addresses[count++]=cursor;size_t size=(sizeof(*tag)+tag->length*4+_Alignof(RenderNativeTag)-1)&~(size_t)(_Alignof(RenderNativeTag)-1);
            if(size>(size_t)(buffer->data-cursor)){valid=0;break;}cursor+=size;
        }
        if(!valid || cursor!=buffer->data){valid=0;break;}
        SHADER_PUT((u16)result,2);SHADER_PUT(count,2);
        for(unsigned i=0;i<count;++i){RenderNativeTag *tag=(RenderNativeTag *)addresses[i];SHADER_PUT(tag->length,1);
            for(unsigned byte=0;byte<tag->length*4;++byte)SHADER_PUT(addresses[i][sizeof(*tag)+byte],1);
            if(tag->length==1 && ((FxCommonDrawModePacket *)tag)->command>UINT32_MAX)valid=0;}
        const unsigned depths[]={0,9,10,4095};
        for(unsigned d=0;d<4 && valid;++d){unsigned depth=depths[d];SHADER_PUT(depth,2);uintptr_t link=ot.entries[depth].address;unsigned steps=0;
            while(link!=0xffffff){unsigned id=0;while(id<count && link!=(uintptr_t)addresses[id])++id;
                if(id==count){if(link==(uintptr_t)&g_FxCommonShadeQuads[0])id=0x8000;else if(link==(uintptr_t)&g_FxCommonShadeQuads[1])id=0x8001;
                    else if(link==(uintptr_t)&g_FxCommonDrawModes1[0])id=0x8002;else if(link==(uintptr_t)&g_FxCommonDrawModes1[1])id=0x8003;else{valid=0;break;}}
                SHADER_PUT(id,2);RenderNativeTag *tag=(RenderNativeTag *)link;
                if(id>=0x8000){SHADER_PUT(tag->length,1);for(unsigned byte=0;byte<tag->length*4;++byte)SHADER_PUT(((u8 *)(tag+1))[byte],1);}
                link=tag->address;if(++steps>count+4){valid=0;break;}
            }
            SHADER_PUT(65535,2);
        }
        for(unsigned i=0;i<4096;++i)if(ot.entries[i].length!=0xab)valid=0;
        for(u8 *at=buffer->data;at<buffer->end;++at)if(*at!=0xa5)valid=0;
        if(arena.before!=UINT64_C(0xa5a5a5a5a5a5a5a5) || arena.after!=UINT64_C(0xa5a5a5a5a5a5a5a5) ||
           ot.before!=UINT64_C(0xa5a5a5a5a5a5a5a5) || ot.after!=UINT64_C(0xa5a5a5a5a5a5a5a5))valid=0;
        ++cases;packets+=count;
    }
#undef SHADER_PUT
    if(!valid || cases!=2118 || packets!=7028 || hash!=UINT64_C(0xe11d5d9a899124c5)){
        fprintf(stderr,"Original FX shaders: cases=%u packets=%u hash=%016llx valid=%d\n",cases,packets,(unsigned long long)hash,valid);return 0;}
    puts("Original FX shaders: eight initialized gradients/six modes and 2118 USA scenarios match fades, marker labels, selection palettes, night shade, both buffers, native payloads and full OT chains");return 1;
}

static int original_fx_shader_pixels(void)
{
    static u16 saved_pixels[1024*512],pixels[1024*512];RECT whole={0,0,1024,512};
    Pe1GameState saved=g_GameState;FxCommonFrame frames[2];memcpy(frames,g_FxCommonFrames,sizeof(frames));
    FxCommonBuffer *active=g_FxCommonActiveBuffer;int slot_before=g_ActiveDrawSlot;
    DRAWENV draw_before;DISPENV display_before;GetDrawEnv(&draw_before);GetDispEnv(&display_before);
    StoreImage(&whole,saved_pixels);DrawSync(0);Boot_InitMemoryLayout();FxCommon_InitDisplay();
    const unsigned intensities[]={0,32,127,255};int valid=1;
    for(unsigned slot=0;slot<2 && valid;++slot)for(unsigned value=0;value<4 && valid;++value){
        for(unsigned i=0;i<1024*512;++i)pixels[i]=0x1357;LoadImage(&whole,pixels);DrawSync(0);
        RECT area={0,(s16)(slot*240),320,240};ClearImage(&area,160,160,160);DrawSync(0);
        g_FxCommonActiveBuffer=&g_FxCommonFrames[slot].buffer;g_ActiveDrawSlot=slot;FxCommon_ClearActiveOrdering();
        g_FxCommonFrames[slot].drawEnv.dtd=0;PutDrawEnv(&g_FxCommonFrames[slot].drawEnv);PutDispEnv(&g_FxCommonFrames[slot].dispEnv);
        FxCommon_DrawFullScreenFade(intensities[value]);if(g_FxCommonActiveBuffer->error){valid=0;break;}
        DrawOTag(&g_FxCommonActiveBuffer->allocation[4095]);DrawSync(0);StoreImage(&whole,pixels);DrawSync(0);
        int channel=20-(int)(intensities[value]>>3);if(channel<0)channel=0;u16 expected=(u16)(channel|(channel<<5)|(channel<<10));
        for(unsigned y=0;y<512 && valid;++y)for(unsigned x=0;x<1024;++x){u16 wanted=x<320 && y>=slot*240 && y<(slot+1)*240?expected:0x1357;
            if(pixels[y*1024+x]!=wanted){fprintf(stderr,"FX fade pixels slot=%u intensity=%u x=%u y=%u got=%04x expected=%04x\n",slot,intensities[value],x,y,pixels[y*1024+x],wanted);valid=0;break;}}
    }
    LoadImage(&whole,saved_pixels);PutDrawEnv(&draw_before);PutDispEnv(&display_before);DrawSync(0);
    g_GameState=saved;memcpy(g_FxCommonFrames,frames,sizeof(frames));g_FxCommonActiveBuffer=active;g_ActiveDrawSlot=slot_before;
    if(valid)puts("Original FX fade pixels: original boot memory/display/arena path, both frame buffers, four subtractive intensities and all 1024x512 VRAM pixels per frame pass headlessly");return valid;
}

static int original_fx_uncompressed_order(void)
{
    static RenderNativeTag ordering[2][4096];static FxCommonDrawModePacket packets[4096];
    const unsigned counts[]={0,1,2,10,127,4096};uint64_t hash=UINT64_C(14695981039346656037);unsigned cases=0;
    for(unsigned slot=0;slot<2;++slot)for(unsigned pattern=0;pattern<128;++pattern){
        unsigned count=counts[pattern%6];ClearOTagR(ordering[slot],4096);
        uintptr_t terminal=ordering[slot][0].address;
        for(unsigned j=0;j<count;++j){unsigned depth=pattern%4==0?0:pattern%4==1?4095:pattern%4==2?j%4096:(j*1103515245u+pattern*12345u)%4096;
            packets[j].tag=(RenderNativeTag){ordering[slot][depth].address,1};packets[j].command=j;
            ordering[slot][depth].address=(uintptr_t)&packets[j];}
        for(unsigned byte=0;byte<4;++byte)hash=(hash^(u8)(count>>(byte*8)))*UINT64_C(1099511628211);
        uintptr_t link=(uintptr_t)&ordering[slot][4095];unsigned steps=0,seen=0;
        while(link!=0xffffff){RenderNativeTag *tag=(RenderNativeTag *)link;
            if(link==terminal){if(tag->address!=0xffffff || tag->length!=4)return 0;break;}
            if(tag->length){uintptr_t offset=link-(uintptr_t)packets;
                if(offset>=count*sizeof(*packets) || offset%sizeof(*packets))return 0;
                unsigned id=offset/sizeof(*packets);for(unsigned byte=0;byte<2;++byte)hash=(hash^(u8)(id>>(byte*8)))*UINT64_C(1099511628211);++seen;}
            link=tag->address;if(++steps>4096+count+1)return 0;
        }
        if(seen!=count)return 0;++cases;
    }
    if(cases!=256 || hash!=UINT64_C(0x8eff7149a4f01905)){fprintf(stderr,"Original FX OT order: cases=%u hash=%016llx\n",cases,(unsigned long long)hash);return 0;}
    puts("Original FX OT order: 256 empty/sparse/dense patterns preserve the same packet order as USA DMA compaction with native OT entries left intact");return 1;
}

static int original_fx_shader_guards(void)
{
    FxCommonBuffer *old_buffer=g_FxCommonActiveBuffer;
    static _Alignas(16) u8 arena[4096];static RenderNativeTag ordering[4096];
    const size_t fade_size=sizeof(FxCommonGradientQuad)+sizeof(FxCommonDrawModePacket);
    unsigned cases=0;int valid=1;
    for(size_t remaining=0;remaining<=fade_size;++remaining){
        memset(arena,0xa5,sizeof(arena));memset(ordering,0xa5,sizeof(ordering));
        FxCommonBuffer buffer={.data=arena,.allocation=ordering,.end=arena+remaining};g_FxCommonActiveBuffer=&buffer;
        FxCommon_DrawFullScreenFade(127);
        if(remaining<fade_size){if(buffer.error!=-1 || buffer.data!=arena)valid=0;
            for(unsigned i=0;i<sizeof(arena);++i)if(arena[i]!=0xa5)valid=0;
            for(unsigned i=0;i<sizeof(ordering);++i)if(((u8 *)ordering)[i]!=0xa5)valid=0;
        }else if(buffer.error || buffer.data!=arena+fade_size)valid=0;
        ++cases;
    }
    const unsigned counts[]={1,4,10};
    for(unsigned slot=0;slot<2;++slot)for(unsigned c=0;c<3;++c)for(unsigned space=0;space<3;++space){
        size_t required=counts[c]*(sizeof(FxCommonLinePacket)+2*sizeof(FxCommonMarkerQuad));
        size_t remaining=space==0?0:space==1?required-1:required;
        memset(arena,0xa5,sizeof(arena));memset(ordering,0xa5,sizeof(ordering));
        for(unsigned i=0;i<10;++i)g_FxCommonEffectSetup[i].enabled=i<counts[c];
        FxCommonBuffer *buffer=&g_FxCommonFrames[slot].buffer;*buffer=(FxCommonBuffer){.data=arena,.allocation=ordering,.end=arena+remaining};g_FxCommonActiveBuffer=buffer;
        if(FxCommon_DrawEffectMarkers(127)!=135)valid=0;
        if(space<2){if(buffer->error!=-1 || buffer->data!=arena)valid=0;
            for(unsigned i=0;i<sizeof(arena);++i)if(arena[i]!=0xa5)valid=0;
            for(unsigned i=0;i<sizeof(ordering);++i)if(((u8 *)ordering)[i]!=0xa5)valid=0;
        }else if(buffer->error || buffer->data!=arena+required)valid=0;
        ++cases;
    }
    FxCommonBuffer buffer={.data=arena,.allocation=ordering,.end=arena};g_FxCommonActiveBuffer=&buffer;
    if(FxCommon_DrawEffectMarkers(0) || buffer.error || buffer.data!=arena)valid=0;++cases;
    g_FxCommonActiveBuffer=old_buffer;
    if(!valid){fprintf(stderr,"Original FX shader guards: cases=%u\n",cases);return 0;}
    printf("Original FX shader guards: %u exact-capacity/short-arena cases, atomic errors, both marker buffers and zero-level mode-only drawing pass\n",cases);return 1;
}

static int original_fx_blend(void)
{
    const s16 counters[]={-32768,-1,0,1,2,31,32,32767};uint64_t hash=UINT64_C(14695981039346656037);unsigned cases=0;
    for(unsigned c=0;c<8;++c)for(unsigned pattern=0;pattern<16;++pattern){
        u32 positions[40],deltas[40];for(unsigned j=0;j<40;++j){positions[j]=j*0x01234567u+pattern*0x9e3779b9u;deltas[j]=~(j*0x07654321u)+pattern*0x13579bdfu;}
        memcpy(g_FxCommonMotionPositions,positions,sizeof(positions));memcpy(g_FxCommonMotionDeltas,deltas,sizeof(deltas));g_FxCommonMotionBlendFrames=counters[c];
        for(unsigned step=0;step<34;++step){FxCommon_StepMotionBlend();
            const u8 *parts[]={(const u8 *)&g_FxCommonMotionBlendFrames,(const u8 *)g_FxCommonMotionPositions};const unsigned sizes[]={2,160};
            for(unsigned p=0;p<2;++p)for(unsigned b=0;b<sizes[p];++b)hash=(hash^parts[p][b])*UINT64_C(1099511628211);
            if(memcmp(g_FxCommonMotionDeltas,deltas,sizeof(deltas)))return 0;++cases;
        }
    }
    if(cases!=4352 || hash!=UINT64_C(0xfd879abe57c21976)){fprintf(stderr,"Original FX blend: cases=%u hash=%016llx\n",cases,(unsigned long long)hash);return 0;}
    puts("Original FX blend: 4352 USA updates match all ten shared motion records, signed/wrapped additions, counter expiry and untouched padding/deltas");return 1;
}

static uint64_t fx_camera_motion_hash(uint64_t hash,int path)
{
    const void *fields[]={&g_FxCommonCameraPosition,&g_FxCommonCameraTarget,
        &g_FxCommonMotionStep0,&g_FxCommonMotionAccum0,&g_FxCommonMotionOrigin0,&g_FxCommonMotionTarget0,&g_FxCommonMotionShift0,&g_FxCommonMotionRemaining0,
        &g_FxCommonMotionStep1,&g_FxCommonMotionAccum1,&g_FxCommonMotionOrigin1,&g_FxCommonMotionTarget1,&g_FxCommonMotionShift1,&g_FxCommonMotionRemaining1,&g_FxCommonPathWrapped};
    const unsigned sizes[]={12,12,12,12,6,6,2,2,12,12,6,6,2,2,1};
    const void *path_fields[]={&g_FxCommonCameraPosition,&g_FxCommonCameraTarget,&g_FxCommonMotionRemaining0,&g_FxCommonMotionRemaining1,
        &g_FxCommonMarkerFade,&g_FxCommonCameraPathRemaining,&g_FxCommonCameraPathStep,&g_FxCommonCameraPathIndex,&g_FxCommonPathWrapped};
    const unsigned path_sizes[]={12,12,2,2,2,1,1,1,1};
    for(unsigned i=0;i<(path?9:15);++i){const u8 *bytes=path?(const u8 *)path_fields[i]:(const u8 *)fields[i];
        for(unsigned j=0;j<(path?path_sizes[i]:sizes[i]);++j){hash=(hash^bytes[j])*UINT64_C(1099511628211);}}
    return hash;
}

static void fx_seed_camera_motion(unsigned pose,FxCommonShortVec3 **shorts)
{
    const FxCommonVec3 positions[]={{0,0,0},{100,-200,300},{INT32_MAX,INT32_MIN,-1},{-32768,32767,0x12345678}};
    const FxCommonVec3 targets[]={{1000,-1000,500},{32767,-32768,100},{-1000000,2000000,-3000000},{INT32_MIN,INT32_MAX,-0x1234567}};
    g_FxCommonCameraPosition=positions[pose];g_FxCommonCameraTarget=targets[pose];g_FxCommonPathWrapped=pose%2;
    for(unsigned j=0;j<6;++j)*shorts[j]=(FxCommonShortVec3){(s16)(-30000+(int)j*701),(s16)(29000-(int)j*503),(s16)(-12345+(int)j*997)};
    memset(&g_FxCommonMotionStep0,0xa5,sizeof(g_FxCommonMotionStep0));memset(&g_FxCommonMotionStep1,0xa5,sizeof(g_FxCommonMotionStep1));
    memset(&g_FxCommonMotionAccum0,0xa5,sizeof(g_FxCommonMotionAccum0));memset(&g_FxCommonMotionAccum1,0xa5,sizeof(g_FxCommonMotionAccum1));
    g_FxCommonMotionShift0=g_FxCommonMotionShift1=g_FxCommonMotionRemaining0=g_FxCommonMotionRemaining1=(s16)0xa5a5;
}

static int original_fx_scene_init(PeDisc *disc)
{
    PeDiscFile archive,exe;
    if(!pe_disc_find(disc,"PE.IMG",&archive) || !pe_disc_find(disc,"SLUS_006.62",&exe) ||
       !pe_disc_read(disc,&exe,0x930d8-0x10000+0x800+76*2,g_PeImageSectorTable+76,10))return fprintf(stderr,"FX scene failure at line %d\n",__LINE__),0;
    if(!SDL_Init(SDL_INIT_EVENTS))return fprintf(stderr,"FX scene failure at line %d\n",__LINE__),0;ResetGraph(0);Boot_InitMemoryLayout();
    CdRom_BindImage(disc,&archive);g_GameState.pe_image_base_lba=archive.sector;
    fx_image_io=1;glyph_reads=glyph_polls=fx_read_failure=fx_poll_failure=0;
    const unsigned rooms[]={0xb7,0xa1,0x5d,0x104,0x7d,0x122,0x179,0x2e,0x3a,0xbf};
    const unsigned areas[]={0,0x80,0x208,0xb8,0xc0,0xd0,0xd8,0xe0,0xe4,0x148,0x160,0x178,0x180,0x1c0,0x1c8};
    const unsigned flags[]={0,0xffffff,0x555555};
    uint64_t hash=UINT64_C(14695981039346656037);unsigned cases=0;
#define SCENE_PUT(value,bytes) do {uint64_t v_=(uint64_t)(value); \
    for(unsigned b_=0;b_<(bytes);++b_){hash=(hash^(u8)v_)*UINT64_C(1099511628211);v_>>=8;}}while(0)
    for(unsigned variant=0;variant<10;++variant)for(unsigned night=0;night<2;++night)
    for(unsigned flag=0;flag<3;++flag)for(unsigned area=0;area<15;++area){
        g_ScriptVariableBank[SCRIPT_VAR_ROOM]=rooms[variant];g_ScriptVariableBank[SCRIPT_VAR_AREA]=areas[area];
        g_ScriptVariableBank[SCRIPT_VAR_FLAGS]=(flags[flag]&~0x2000u)|(night?0x2000u:0);
        /* Re-entry must not inherit animation or reserved fields from the
         * previous visit; the original would have reloaded the overlay. */
        memset(&g_FxCommonRuntime,0xa5,sizeof(g_FxCommonRuntime));
        memset(g_FxCommonEffectSetup,0xa5,sizeof(g_FxCommonEffectSetup));
        memset(g_FxCommonMotionPositions,0xa5,sizeof(g_FxCommonMotionPositions));
        int result=FxCommon_InitScene();
        if(result || g_FxCommonRouteNodeCount!=132){fprintf(stderr,"FX scene init case %u result=%d routes=%d\n",cases,result,g_FxCommonRouteNodeCount);return fprintf(stderr,"FX scene failure at line %d\n",__LINE__),0;}
        for(unsigned i=0;i<10;++i)if(g_FxCommonRuntime.pairedNodes[i]!=&g_FxCommonRecords[2*i] ||
           g_FxCommonRuntime.pairedNodes[10+i]!=&g_FxCommonRecords[2*i+1])return fprintf(stderr,"FX scene failure at line %d\n",__LINE__),0;
        for(unsigned i=0;i<11;++i)if(g_FxCommonSceneNodes[i]!=&g_FxCommonRecords[22+i])return fprintf(stderr,"FX scene failure at line %d\n",__LINE__),0;
        for(unsigned i=0;i<132;++i)if(g_FxCommonRouteNodes[i]!=&g_FxCommonRecords[55+i])return fprintf(stderr,"FX scene failure at line %d\n",__LINE__),0;
        if(g_FxCommonRuntime.node0!=&g_FxCommonRecords[20] || g_FxCommonType51Node!=&g_FxCommonRecords[21] ||
           g_FxCommonRuntime.node25!=&g_FxCommonRecords[33] || g_FxCommonType54Node!=&g_FxCommonRecords[34])return fprintf(stderr,"FX scene failure at line %d\n",__LINE__),0;
        const unsigned path_ids[]={37,39,38,40,35,36},echo_ids[]={41,43,42,44};
        for(unsigned i=0;i<6;++i)if(g_FxCommonPathNodes[i]!=&g_FxCommonRecords[path_ids[i]])return fprintf(stderr,"FX scene failure at line %d\n",__LINE__),0;
        for(unsigned i=0;i<4;++i)if(g_FxCommonEchoNodes[i]!=&g_FxCommonRecords[echo_ids[i]] ||
           g_FxCommonType26Nodes[i]!=&g_FxCommonRecords[47+i])return fprintf(stderr,"FX scene failure at line %d\n",__LINE__),0;
        for(unsigned i=0;i<2;++i)if(g_FxCommonType21Nodes[i]!=&g_FxCommonRecords[45+i] ||
           g_FxCommonLodNodes[i]!=&g_FxCommonRecords[51+i] || g_FxCommonPairedNodes[i]!=&g_FxCommonRecords[53+i])return fprintf(stderr,"FX scene failure at line %d\n",__LINE__),0;
        for(unsigned i=0;i<sizeof(g_FxCommonEffectSetup);++i)SCENE_PUT(((u8 *)g_FxCommonEffectSetup)[i],1);
        SCENE_PUT(g_FxCommonRouteNodeCount,4);
        FxCommonNode *ordered[200],*node;unsigned count=0;FxCommon_BeginRecordIteration();
        while((node=FxCommon_NextRecord())){if(count>=200)return fprintf(stderr,"FX scene failure at line %d\n",__LINE__),0;ordered[count++]=node;}
        SCENE_PUT(count,2);if(count!=187)return fprintf(stderr,"FX scene failure at line %d\n",__LINE__),0;
        for(unsigned i=0;i<count;++i){node=ordered[i];unsigned ident=node->id,kind=ident<20 || ident>=51?2:ident==20?0:1;
            SCENE_PUT(ident,2);SCENE_PUT(kind,1);SCENE_PUT(node->type,2);
            if(kind==2){SCENE_PUT(node->middleKind,2);SCENE_PUT(node->nearKind,2);SCENE_PUT(node->lod.margin,2);
                SCENE_PUT(node->lod.farDepth,4);SCENE_PUT(node->lod.nearDepth,4);}
            else if(kind==1)SCENE_PUT(node->margin,2);
            SCENE_PUT((u8 *)node->resource-(u8 *)g_FxCommonModelTable,4);
            for(unsigned j=0;j<3;++j)SCENE_PUT(kind==2?node->lod.matrix.t[j]:node->matrix.t[j],4);
            for(unsigned j=0;j<3;++j)SCENE_PUT(kind==2?node->lod.seed.component[j]:node->seed.component[j],2);
            SCENE_PUT(node->bucket,2);SCENE_PUT(fx_node_id(node->parent),2);
            SCENE_PUT(fx_node_id(node->previous),2);SCENE_PUT(fx_node_id(node->next),2);
        }
        SCENE_PUT(g_FxCommonRoomVariant,2);SCENE_PUT(g_FxCommonViewMode,2);
        SCENE_PUT(g_FxCommonPlaying,1);SCENE_PUT(g_FxCommonPlayAdvance,1);SCENE_PUT(g_FxCommonReplayCamera,1);
        SCENE_PUT(g_FxCommonPlayTicks,4);SCENE_PUT(g_FxCommonPlayFrame,4);SCENE_PUT(D_8019C00D,1);SCENE_PUT(D_8019C02E,2);
        s32 *positions[]={&g_FxCommonCameraPosition.x,&g_FxCommonCameraTarget.x};
        for(unsigned i=0;i<2;++i)for(unsigned j=0;j<3;++j)SCENE_PUT(positions[i][j],4);
        for(unsigned i=0;i<sizeof(g_FxCommonMotionPositions);++i)SCENE_PUT(((u8 *)g_FxCommonMotionPositions)[i],1);
        ++cases;
    }
#undef SCENE_PUT
    for(unsigned failure=0;failure<4;++failure){
        CdRom_BindImage(disc,&archive);
        glyph_reads=glyph_polls=0;fx_read_failure=failure<2?failure+1:0;fx_poll_failure=failure>=2?failure-1:0;
        g_FxCommonRouteNodeCount=123;
        if(FxCommon_InitScene()!=-1 || g_FxCommonRouteNodeCount!=123 ||
           glyph_reads!=failure%2+1 || glyph_polls!=(failure<2?failure:failure-1))return fprintf(stderr,"FX scene failure at line %d\n",__LINE__),0;
    }
    CdRom_BindImage(disc,&archive);
    glyph_reads=glyph_polls=fx_read_failure=fx_poll_failure=0;
    if(FxCommon_InitScene()!=0 || g_FxCommonRouteNodeCount!=132)return fprintf(stderr,"FX scene failure at line %d\n",__LINE__),0;
    fx_image_io=0;
    if(cases!=900 || hash!=UINT64_C(0x8a8774df804ba8b5)){fprintf(stderr,"Original FX InitScene: cases=%u hash=%016llx\n",cases,(unsigned long long)hash);return fprintf(stderr,"FX scene failure at line %d\n",__LINE__),0;}
    puts("Original FX InitScene: 900 USA scene graphs, all ten room variants, both day/night model banks, three flag patterns and 15 entry areas, all node slots, four I/O failures and recovery pass with real image I/O");return 1;
}

static int original_fx_controller(void)
{
    uint64_t hash=UINT64_C(14695981039346656037);unsigned cases=0;
    const unsigned rooms[]={0xb7,0xa1,0x5d,0x104,0x7d,0x122,0x179,0x2e,0x3a,0xbf};
    for(unsigned scenario=0;scenario<44;++scenario){
        unsigned variant=scenario<40?scenario/4:0,night=scenario<40?(scenario/2)%2:(scenario-40)/2;
        unsigned script=scenario<40?scenario%2:2+(scenario%2);
        g_ScriptVariableBank[SCRIPT_VAR_ROOM]=rooms[variant];
        g_ScriptVariableBank[SCRIPT_VAR_AREA]=script==2?0x80:script==3?0x208:0;
        g_ScriptVariableBank[SCRIPT_VAR_FLAGS]=0xffdfff|(night?0x2000:0);g_GameState.flags=0;
        fx_image_io=1;glyph_reads=glyph_polls=fx_read_failure=fx_poll_failure=0;
        if(FxCommon_InitScene()!=0)return 0;
        for(unsigned frame=0;frame<(script==2?1230:512);++frame){
            u32 pressed=0;
            if(script==0){switch(frame%80){case 0:pressed=0x40;break;case 20:pressed=0x22;break;case 40:pressed=0x10;break;case 60:pressed=0x50;break;}}
            else if(script==1 || script==3){if(frame==0 || frame==300 || frame==400)pressed=0x20000000;else if(frame==240)pressed=0x40000000;}
            g_FieldPadPressed=g_FieldPadBits=pressed;
            FxCommon_StepEffects();FxCommon_StepPathNodes();FxCommon_StepMotionBlend();
            if(g_FxCommonActiveBuffer->error)return 0;
            const void *fields[]={&g_FxCommonPageChanged,&g_FxCommonPageOpen,&g_FxCommonExitRequested,&g_FxCommonExitCountdown,
                &g_FxCommonPageId,&g_FxCommonScreenFade,&g_FxCommonDemoFrame,&g_FxCommonDrawTintedCopies,
                &g_FxCommonTailPathPosition,&g_FxCommonFlyerPathPosition,&g_FxCommonCameraPathPosition,&g_FxCommonDemoTicks,
                &g_FxCommonDemoExiting,&g_FxCommonDemoResource,&g_FxCommonDemoResourceTicks,&g_FxCommonDemoShade,&g_FxCommonDemoFadePhase,
                &g_FxCommonInputLock,&g_FxCommonTailViewEntered,&g_FxCommonExitVariant,&g_FxCommonResetRequested,D_8019C0D0,&g_FxCommonResourceState,
                &g_FxCommonRoomVariant,&g_FxCommonViewMode,&g_FxCommonPlaying,&g_FxCommonPlayAdvance,&g_FxCommonReplayCamera,
                &g_FxCommonPlayTicks,&g_FxCommonPlayFrame,&D_8019C00D,&D_8019C02E};
            const unsigned sizes[]={4,4,2,2,2,2,2,2,4,4,4,4,4,2,4,1,1,2,2,4,1,4,8,2,2,1,1,1,4,4,1,2};
            for(unsigned i=0;i<sizeof(sizes)/sizeof(*sizes);++i)for(unsigned j=0;j<sizes[i];++j){hash=(hash^((const u8 *)fields[i])[j])*UINT64_C(1099511628211);}
            hash=fx_camera_motion_hash(hash,0);
            const void *path_fields[]={&g_FxCommonMarkerFade,&g_FxCommonCameraPathRemaining,&g_FxCommonCameraPathStep,&g_FxCommonCameraPathIndex,&g_FxCommonMotionBlendFrames,g_FxCommonMotionPositions,g_FxCommonMotionDeltas};
            const unsigned path_sizes[]={2,1,1,1,2,160,160};
            for(unsigned i=0;i<7;++i)for(unsigned j=0;j<path_sizes[i];++j){hash=(hash^((const u8 *)path_fields[i])[j])*UINT64_C(1099511628211);}
            for(unsigned i=0;i<6;++i){const void *parts[]={g_FxCommonPathNodes[i]->matrix.t,g_FxCommonPathNodes[i]->seed.component};
                for(unsigned part=0;part<2;++part)for(unsigned byte=0;byte<(part?6:12);++byte){hash=(hash^((const u8 *)parts[part])[byte])*UINT64_C(1099511628211);}}
            ++cases;if(g_FxCommonExitRequested)break;
        }
    }
    fx_image_io=0;
    if(cases!=21624 || hash!=UINT64_C(0xe9ce596e8e1fa6c1)){fprintf(stderr,"FX controller: %u states hash=%016llx\n",cases,(unsigned long long)hash);return 0;}
    fx_image_io=1;
    for(unsigned fault=0;fault<4;++fault){
        g_ScriptVariableBank[SCRIPT_VAR_AREA]=0;
        if(FxCommon_InitScene()!=0)return 0;
        g_FieldPadPressed=g_FieldPadBits=fault%2?0x10:0x40;
        if(fault<2){for(unsigned i=0;i<10;++i)g_FxCommonEffectSetup[i].enabled=0;}
        else if(fault%2)g_FxCommonEffectSetup[g_FxCommonRoomVariant].value23=254;
        else g_FxCommonEffectSetup[g_FxCommonRoomVariant].value22=254;
        FxCommon_StepEffects();if(g_FxCommonActiveBuffer->error!=-3)return 0;
    }
    fx_image_io=0;
    puts("Original FX controller: 21624 USA states across 44 browsing/play/cancel/demo/path sequences, all playback/motion/resource fields and six animated nodes match; four invalid selection graphs terminate");return 1;
}

static unsigned fx_loop_vblanks,fx_loop_frames,fx_loop_packets,fx_loop_pixels,fx_loop_failures,fx_loop_demo;
static u16 fx_loop_image[320*480];
static void fx_loop_observe(void)
{
    Boot_VsyncCallback();++fx_loop_vblanks;
    if(fx_loop_vblanks>3000){++fx_loop_failures;g_FxCommonExitRequested=1;return;}
    if(!(fx_loop_vblanks&1))return;
    FxCommonBuffer *buffer=g_FxCommonActiveBuffer;
    u8 *base=g_RenderScratchBufferBase[0]+g_ActiveDrawSlot*sizeof(FxCommonBufferRegion);
    if(buffer->data==base)return; /* EndSequence's final 30 blanks. */
    ++fx_loop_frames;
    if(buffer->data<base || buffer->data>buffer->end || buffer->error){++fx_loop_failures;g_FxCommonExitRequested=1;return;}
    uintptr_t link=(uintptr_t)&buffer->allocation[4095];unsigned steps=0,packets=0;
    while(link!=0xffffff){
        RenderNativeTag *tag=(RenderNativeTag *)link;
        if(link<0x10000 || link%_Alignof(RenderNativeTag) || ++steps>65536 || tag->length>16){++fx_loop_failures;break;}
        if(link>=(uintptr_t)base && link<(uintptr_t)buffer->end &&
           link+sizeof(*tag)+tag->length*4>(uintptr_t)buffer->data){++fx_loop_failures;break;}
        if(tag->length)++packets;link=tag->address;
    }
    if(packets<10)++fx_loop_failures;fx_loop_packets+=packets;
    if(fx_loop_frames%20==0){
        RECT area={0,0,320,480};StoreImage(&area,fx_loop_image);DrawSync(0);
        unsigned visible=0;for(unsigned i=0;i<320*480;++i)if(fx_loop_image[i]&0x7fff)++visible;
        if(visible>10000)++fx_loop_pixels;
    }
    if(!fx_loop_demo){
        /* Exercise the original SDL->pad packet->field input path. Two
         * confirmations start the playback and then request a normal exit. */
        if(fx_loop_frames==20 || fx_loop_frames==100)host_event(SDL_EVENT_KEY_DOWN,SDL_SCANCODE_X);
        if(fx_loop_frames==21 || fx_loop_frames==101)host_event(SDL_EVENT_KEY_UP,SDL_SCANCODE_X);
        if(fx_loop_frames>200){++fx_loop_failures;g_FxCommonExitRequested=1;}
    }
}

static int original_fx_frame_loop(PeDisc *disc)
{
    /* The original loop produces queued AKAO commands, as it does in-game. */
    Spu_InitHardware();
    if(Psyz_AudioInit()<0)return 0;
    PeDiscFile archive,exe;u16 sectors[5];
    if(!pe_disc_find(disc,"PE.IMG",&archive) || !pe_disc_find(disc,"SLUS_006.62",&exe) ||
       !pe_disc_read(disc,&exe,0x930d8-0x10000+0x800+72*2,sectors,sizeof(sectors)))return 0;
    /* Scene_LoadSceneData loads the common SF0 instrument/sample bank before
     * any real AKAO consumer can play the authored FX sound sequences. */
    u16 sound_sectors[2];
    if(!pe_disc_read(disc,&exe,0x930d8-0x10000+0x800+6*2,sound_sectors,sizeof(sound_sectors)) ||
       sound_sectors[1]<=sound_sectors[0])return 0;
    size_t sound_bytes=(size_t)(sound_sectors[1]-sound_sectors[0])*2048;
    u8 *sound=malloc(sound_bytes);if(!sound)return 0;
    int loaded=pe_disc_read(disc,&archive,(uint64_t)sound_sectors[0]*2048,sound,sound_bytes) &&
               Spu_UploadSampleBlockBlocking(sound,1)==0;
    free(sound);if(!loaded)return 0;
    u8 *textures=malloc(0x80000);if(!textures)return 0;
    unsigned total_frames=0,total_packets=0;
    for(unsigned night=0;night<2;++night)for(unsigned demo=0;demo<2;++demo){
        for(unsigned bank=0;bank<2;++bank){unsigned index=bank?(night?1:2):0;
            if(!pe_disc_read(disc,&archive,(uint64_t)sectors[index]*2048,textures,(sectors[index+1]-sectors[index])*2048u)){free(textures);return 0;}
            Draw_LoadTimTable((TimRelativeTable *)textures,bank?262:3);DrawSync(0);
        }
        Boot_BuildRenderFlagTable();Boot_ResetPadInput();g_FieldPadBits=g_FieldPadPressed=0;
        g_GameState.flags=0;
        g_ScriptVariableBank[SCRIPT_VAR_AREA]=demo?0x80:0;
        g_ScriptVariableBank[SCRIPT_VAR_ROOM]=0xb7;
        g_ScriptVariableBank[SCRIPT_VAR_FLAGS]=0xffdfff|(night?0x2000:0);
        CdRom_BindImage(disc,&archive);
        fx_image_io=1;glyph_reads=glyph_polls=fx_read_failure=fx_poll_failure=0;
        fx_loop_vblanks=fx_loop_frames=fx_loop_packets=fx_loop_pixels=fx_loop_failures=0;fx_loop_demo=demo;
        VSyncCallback(fx_loop_observe);int result=FxCommon_RunFrameLoop();VSyncCallback(NULL);fx_image_io=0;
        fprintf(stderr,"FX full loop night=%u demo=%u result=%d frames=%u packets=%u visible=%u errors=%u\n",night,demo,result,fx_loop_frames,fx_loop_packets,fx_loop_pixels,fx_loop_failures);
        if(result || fx_loop_failures || fx_loop_frames!=(demo?1216u:116u) || !fx_loop_pixels ||
           g_FxCommonResetRequested || !g_FxCommonExitRequested){free(textures);return 0;}
        total_frames+=fx_loop_frames;total_packets+=fx_loop_packets;
    }
    free(textures);Spu_Shutdown();Psyz_AudioDestroy();
    printf("Original FX frame loop: %u frames, %u linked packets, day/night original texture banks, original SDL input/playback and complete demo exits, both buffers and headless pixels pass\n",total_frames,total_packets);return 1;
}

static int original_fx_camera_motion(PeDisc *disc)
{
    PeDiscFile archive;if(!pe_disc_find(disc,"PE.IMG",&archive) ||
       !pe_disc_read(disc,&archive,1975u*2048,g_FxCommonTimTable,41*2048))return 0;
    /* Compare authored data beyond the old scanner's 26-sector code slice.
     * The real InitScene loads these addresses, not identity matrices. */
    u8 sceneData[0x68];
    if(!pe_disc_read(disc,&archive,1792u*2048+(0x8019c0d4u-0x8018eff0u),sceneData,sizeof(sceneData)) ||
       memcmp(sceneData,g_FxCommonRouteFarDepths,20) ||
       memcmp(sceneData+20,g_FxCommonRouteNearDepths,20) ||
       memcmp(sceneData+40,&g_FxCommonColorMatrix,32) ||
       memcmp(sceneData+72,&g_FxCommonLightMatrix,32))return 0;
    g_FxCommonPathTailNodes[0]=(FxCommonTransformNode *)&g_FxCommonDefaultNode;
    g_FxCommonPathNodes[5]=(FxCommonTransformNode *)&g_FxCommonDefaultNode;
    if(g_FxCommonPathNodes[4]!=(FxCommonTransformNode *)&g_FxCommonDefaultNode ||
       g_FxCommonPathTailNodes[1]!=(FxCommonTransformNode *)&g_FxCommonDefaultNode)return 0;
    g_FxCommonPathNodes[4]=g_FxCommonPathNodes[5]=NULL;
    FxCommonShortVec3 external[2];FxCommonShortVec3 *shorts[]={external,external+1,&g_FxCommonMotionOrigin0,&g_FxCommonMotionTarget0,&g_FxCommonMotionOrigin1,&g_FxCommonMotionTarget1};
    const s32 shifts[]={0,1,2,14,15,16,31,32,63,255,256,-1};
    const u32 sources[]={0,1,255,256,0xffffff80};
    uint64_t hash=UINT64_C(14695981039346656037);unsigned cases=0;int valid=1;
    for(unsigned pose=0;pose<4;++pose)for(unsigned a=0;a<6;++a)for(unsigned b=0;b<6;++b)for(unsigned d=0;d<12;++d){
        fx_seed_camera_motion(pose,shorts);FxCommon_SetMotionEndpoints(shorts[a],shorts[b],shifts[d],shifts[(d*5+3)%12]);
        hash=fx_camera_motion_hash(hash,0);++cases;
    }
    if(cases!=1728 || hash!=UINT64_C(0x8ad0fc610fb630f5)){fprintf(stderr,"Original camera endpoints: cases=%u hash=%016llx\n",cases,(unsigned long long)hash);return 0;}
    for(unsigned i=0;i<10;++i){g_FxCommonEffectSetup[i].motionIndices[0]=(i*7)%80;g_FxCommonEffectSetup[i].motionIndices[1]=(i*7+1)%80;}
    hash=UINT64_C(14695981039346656037);cases=0;
    for(unsigned pose=0;pose<4;++pose)for(unsigned index=0;index<10;++index)for(unsigned source=0;source<5;++source)for(unsigned d=0;d<12;++d){
        fx_seed_camera_motion(pose,shorts);FxCommon_StartCameraMove(index,shifts[d],shifts[(d*5+3)%12],sources[source]);
        hash=fx_camera_motion_hash(hash,0);++cases;
    }
    if(cases!=2400 || hash!=UINT64_C(0xf71e670bec43b4dd)){fprintf(stderr,"Original sampled camera endpoints: cases=%u hash=%016llx\n",cases,(unsigned long long)hash);return 0;}
    hash=UINT64_C(14695981039346656037);cases=0;
    for(unsigned index=0;index<79;++index)for(unsigned source=0;source<5;++source){
        g_FxCommonMotionRemaining0=123;g_FxCommonMotionRemaining1=-234;g_FxCommonMarkerFade=321;g_FxCommonPathWrapped=index%2;
        g_FxCommonCameraPathRemaining=g_FxCommonCameraPathStep=g_FxCommonCameraPathIndex=0xa5;
        if(FxCommon_InitCameraPath(index+(index%2?0x10000:0),111,-222,(s32)sources[source]))valid=0;
        hash=fx_camera_motion_hash(hash,1);++cases;
        unsigned count=g_FxCommonCameraPathRemaining;
        for(unsigned step=0;step<count+2;++step){int expected=g_FxCommonCameraPathRemaining>1;
            if(FxCommon_StepCameraPath()!=expected)valid=0;hash=fx_camera_motion_hash(hash,1);++cases;}
    }
    for(unsigned index=0;index<79;++index){
        g_FxCommonMotionRemaining0=g_FxCommonMotionRemaining1=g_FxCommonMarkerFade=0;g_FxCommonPathWrapped=0;
        g_FxCommonCameraPathRemaining=3;g_FxCommonCameraPathStep=254;g_FxCommonCameraPathIndex=index;
        for(unsigned step=0;step<4;++step){int expected=g_FxCommonCameraPathRemaining>1;
            if(FxCommon_StepCameraPath()!=expected)valid=0;hash=fx_camera_motion_hash(hash,1);++cases;}
    }
    if(!valid || cases!=47836 || hash!=UINT64_C(0xc29a21ef98b25f06)){fprintf(stderr,"Original camera paths: cases=%u hash=%016llx valid=%d\n",cases,(unsigned long long)hash,valid);return 0;}
    /* Bad path selectors and rejected point counts leave all observable state intact. */
    const int bad[]={-1,79,255,65535,0x1004f};unsigned errors=0;
    for(unsigned i=0;i<5;++i){uint64_t before=fx_camera_motion_hash(UINT64_C(14695981039346656037),1);
        if(FxCommon_InitCameraPath(bad[i],0,0,0)!=-1 || fx_camera_motion_hash(UINT64_C(14695981039346656037),1)!=before)valid=0;++errors;}
    for(unsigned index=79;index<=255;index+=176){g_FxCommonCameraPathIndex=index;g_FxCommonCameraPathRemaining=2;
        uint64_t before=fx_camera_motion_hash(UINT64_C(14695981039346656037),1);
        if(FxCommon_StepCameraPath()!=-1 || fx_camera_motion_hash(UINT64_C(14695981039346656037),1)!=before)valid=0;++errors;}
    u8 *table=(u8 *)Draw_GetTimTableEntry(g_FxCommonTimTable,2);
    for(unsigned index=0;index<2;++index){u32 offset;memcpy(&offset,table+index*4,4);s16 count;memcpy(&count,table+offset+6,2);
        s16 bad_count=2;memcpy(table+offset+6,&bad_count,2);
        g_FxCommonCameraPathIndex=0;g_FxCommonCameraPathStep=255;g_FxCommonCameraPathRemaining=2;g_FxCommonPathWrapped=0;
        uint64_t before=fx_camera_motion_hash(UINT64_C(14695981039346656037),1);
        if(FxCommon_InitCameraPath(0,0,0,-128)!=-1 || fx_camera_motion_hash(UINT64_C(14695981039346656037),1)!=before)valid=0;++errors;
        if(FxCommon_StepCameraPath()!=-1 || fx_camera_motion_hash(UINT64_C(14695981039346656037),1)!=before)valid=0;++errors;
        memcpy(table+offset+6,&count,2);
    }
    if(!valid || errors!=11){fprintf(stderr,"Original camera path guards: errors=%u valid=%d\n",errors,valid);return 0;}
    puts("Original camera motion: 1728 USA aliased-endpoint scenarios, 2400 sampled constructors, all 79 real path pairs through 47836 init/update/hold states, shift/counter wrapping and 11 atomic error cases pass");return 1;
}

static int original_fx_model_render_assets(PeDisc *disc)
{
    PeDiscFile archive;if(!pe_disc_find(disc,"PE.IMG",&archive))return 0;
    static _Alignas(16) u8 bank[100*2048];
    static struct {u64 before;_Alignas(16) u8 bytes[0x40000];u64 after;} arena;
    static struct {u64 before;RenderNativeTag entries[4096];u64 after;} ot;
    const unsigned sectors[]={2016,2105},sizes[]={89,100};
    const s32 poses[][3]={{1000,0,0},{8000,800,0},{-1000,1000,0},{16000,4095,1}};
    void (*draws[])(void *,FxCommonPolyModel *,int)={FxCommon_DrawModel,FxCommon_DrawModelTinted,FxCommon_DrawModelFlat};
    FxCommonBuffer *old_buffer=g_FxCommonActiveBuffer; s32 old_bias=g_FxCommonDepthBias;
    u8 old_color[4];memcpy(old_color,g_FxCommonFlatColor,4);
    FxCommonRenderScratchpad old_work=g_FxCommonRenderWorkspace;
    uint64_t hash=UINT64_C(14695981039346656037);unsigned cases=0,packets=0,coverage[8]={0};int valid=1;
#define MODEL_PUT(value,count) do {uint64_t word_=(uint64_t)(value); \
    for(unsigned byte_=0;byte_<(count);++byte_){hash=(hash^(u8)word_)*UINT64_C(1099511628211);word_>>=8;}}while(0)
    for(unsigned variant=0;variant<2 && valid;++variant){
        if(!pe_disc_read(disc,&archive,(uint64_t)sectors[variant]*2048,bank,sizes[variant]*2048)){valid=0;break;}
        FxCommonPolyModel *models=(FxCommonPolyModel *)(bank+8);
        if(models->bankOffsets[0]!=85*4){valid=0;break;}
        for(unsigned index=0;index<85 && valid;++index)for(unsigned renderer=0;renderer<3 && valid;++renderer)
        for(unsigned pose=0;pose<4 && valid;++pose){
            for(unsigned i=0;i<32;++i){Psyz_GteDataWrite(i,0);Psyz_GteCtrlWrite(i,0);}
            const s32 controls[]={4096,0,poses[pose][2]?-4096:4096,0,4096,0,0,poses[pose][0]};
            for(unsigned i=0;i<8;++i)Psyz_GteCtrlWrite(i,(u32)controls[i]);
            Psyz_GteCtrlWrite(24,160u<<16);Psyz_GteCtrlWrite(25,120u<<16);Psyz_GteCtrlWrite(26,768);
            Psyz_GteCtrlWrite(29,341);Psyz_GteCtrlWrite(30,256);
            memset(&g_FxCommonRenderWorkspace,0xa5,sizeof(g_FxCommonRenderWorkspace));
            memset(&arena,0xa5,sizeof(arena));memset(&ot,0xa5,sizeof(ot));
            for(unsigned i=0;i<4096;++i)ot.entries[i]=(RenderNativeTag){0xffffff,0xab};
            FxCommonBuffer buffer={.data=arena.bytes,.allocation=(RenderNativeTag *)ot.entries,.end=arena.bytes+sizeof(arena.bytes)};
            g_FxCommonActiveBuffer=&buffer;g_FxCommonDepthBias=poses[pose][1];
            memcpy(g_FxCommonFlatColor,(u8[]){40,80,120,0},4);
            draws[renderer](NULL,models,index);
            if(buffer.error || buffer.data<arena.bytes || buffer.data>buffer.end){valid=0;break;}
            u8 *cursor=arena.bytes,*addresses[8192];unsigned count=0;
            while(cursor<buffer.data){RenderNativeTag *tag=(RenderNativeTag *)cursor;
                if(count>=8192 || (tag->length!=4 && tag->length!=5 && tag->length!=6 && tag->length!=7 && tag->length!=8 && tag->length!=9 && tag->length!=12)){valid=0;break;}
                addresses[count++]=cursor;
                size_t size=(sizeof(*tag)+tag->length*4+_Alignof(RenderNativeTag)-1)&~(size_t)(_Alignof(RenderNativeTag)-1);
                if(size>(size_t)(buffer.data-cursor)){valid=0;break;}cursor+=size;
            }
            if(!valid || cursor!=buffer.data){valid=0;break;}
            MODEL_PUT(count,4);
            for(unsigned i=0;i<count;++i){RenderNativeTag *tag=(RenderNativeTag *)addresses[i];MODEL_PUT(tag->length,1);
                const u8 codes[]={0x20,0x28,0x30,0x38,0x24,0x2c,0x34,0x3c};
                unsigned code=addresses[i][sizeof(*tag)+3]&0xfc;
                for(unsigned kind=0;kind<8;++kind)if(code==codes[kind])++coverage[kind];
                for(unsigned byte=0;byte<tag->length*4;++byte)MODEL_PUT(addresses[i][sizeof(*tag)+byte],1);}
            for(unsigned depth=0;depth<4096 && valid;++depth){
                if(ot.entries[depth].length!=0xab){valid=0;break;}
                uintptr_t link=ot.entries[depth].address;if(link==0xffffff)continue;MODEL_PUT(depth,2);unsigned steps=0;
                while(link!=0xffffff){unsigned id=0;while(id<count && (uintptr_t)addresses[id]!=link)++id;
                    if(id==count || ++steps>count){valid=0;break;}MODEL_PUT(id,2);link=((RenderNativeTag *)link)->address;}
                MODEL_PUT(65535,2);
            }
            MODEL_PUT(65535,2);packets+=count;++cases;
            if(arena.before!=UINT64_C(0xa5a5a5a5a5a5a5a5) || arena.after!=UINT64_C(0xa5a5a5a5a5a5a5a5) ||
               ot.before!=UINT64_C(0xa5a5a5a5a5a5a5a5) || ot.after!=UINT64_C(0xa5a5a5a5a5a5a5a5))valid=0;
            for(u8 *p=buffer.data;p<buffer.end;++p)if(*p!=0xa5){valid=0;break;}
        }
    }
#undef MODEL_PUT
    for(unsigned i=0;i<8;++i)if(!coverage[i])valid=0;
    /* A real native packet must fit completely; a bad depth cannot address an OT sentinel. */
    _Alignas(16) u8 small_bank[128]={0};s32 offset=4;memcpy(small_bank,&offset,4);
    FxCommonPolyResource *header=(FxCommonPolyResource *)(small_bank+4);header->counts[0]=1;
    for(unsigned i=0;i<8;++i)header->offsets[i]=52;
    FxCommonModelF3 *triangle=(FxCommonModelF3 *)(small_bank+56);
    triangle->color.word=0x20ffffff;
    triangle->vertices[0]=(GteShortVector){-10,-10,0,0};triangle->vertices[1]=(GteShortVector){10,-10,0,0};triangle->vertices[2]=(GteShortVector){-10,10,0,0};
    for(unsigned renderer=0;renderer<3;++renderer){
        memset(&arena,0xa5,sizeof(arena));memset(&ot,0xa5,sizeof(ot));
        FxCommonBuffer buffer={.data=arena.bytes,.allocation=(RenderNativeTag *)ot.entries,
            .end=arena.bytes+sizeof(FxCommonModelF3Packet)-1};g_FxCommonActiveBuffer=&buffer;
        draws[renderer](NULL,(FxCommonPolyModel *)small_bank,0);
        if(buffer.error!=-1 || buffer.data!=arena.bytes)valid=0;
        for(unsigned i=0;i<sizeof(arena);++i)if(((u8 *)&arena)[i]!=0xa5)valid=0;
        for(unsigned i=0;i<sizeof(ot);++i)if(((u8 *)&ot)[i]!=0xa5)valid=0;
    }
    for(unsigned i=0;i<32;++i){Psyz_GteDataWrite(i,0);Psyz_GteCtrlWrite(i,0);}
    const u32 controls[]={4096,0,4096,0,4096,0,0,1000};
    for(unsigned i=0;i<8;++i)Psyz_GteCtrlWrite(i,controls[i]);Psyz_GteCtrlWrite(26,768);
    Psyz_GteCtrlWrite(29,341);Psyz_GteCtrlWrite(30,256);
    for(unsigned i=0;i<2;++i){memset(&ot,0xa5,sizeof(ot));
        FxCommonBuffer buffer={.data=arena.bytes,.allocation=(RenderNativeTag *)ot.entries,.end=arena.bytes+sizeof(arena.bytes)};
        g_FxCommonActiveBuffer=&buffer;g_FxCommonDepthBias=i?-1:4096;
        FxCommon_DrawModelFlat(NULL,(FxCommonPolyModel *)small_bank,0);
        if(buffer.error!=-2 || buffer.data!=arena.bytes)valid=0;
        for(unsigned j=0;j<sizeof(ot);++j)if(((u8 *)&ot)[j]!=0xa5)valid=0;
    }
    g_FxCommonActiveBuffer=old_buffer;g_FxCommonDepthBias=old_bias;memcpy(g_FxCommonFlatColor,old_color,4);g_FxCommonRenderWorkspace=old_work;
    if(!valid || cases!=2040 || hash!=UINT64_C(0x22489a443c2917c3)){fprintf(stderr,"Original FX models: cases=%u packets=%u hash=%016llx valid=%d\n",cases,packets,(unsigned long long)hash,valid);return 0;}
    puts("Original FX models: all 170 day/night models, three original drawers, all eight polygon streams, four views, native packet payloads and complete ordering chains match USA CPU/GTE; arena/depth guards pass");return 1;
}

static int original_fx_resources(PeDisc *disc,PeDiscFile *exe)
{
    Pe1GameState saved=g_GameState;PeDisc *old_image=g_CdImage;PeDiscFile archive,old_archive;
    u16 saved_sectors[5],sectors[5];u8 old_night=g_FxCommonNight;
    memcpy(saved_sectors,g_PeImageSectorTable+76,sizeof(saved_sectors));
    if(!pe_disc_find(disc,"PE.IMG",&archive) ||
       !pe_disc_read(disc,exe,0x930d8-0x10000+0x800+76*2,sectors,sizeof(sectors)))return 0;
    if(old_image && !pe_disc_find(old_image,"PE.IMG",&old_archive))return 0;
    u8 *expected=malloc(FX_COMMON_RESOURCE_BYTES);if(!expected)return 0;
    int valid=1;unsigned cases=0;uint64_t hash=UINT64_C(14695981039346656037);
    memcpy(g_PeImageSectorTable+76,sectors,sizeof(sectors));fx_image_io=1;
    for(unsigned night=0;night<2 && valid;++night){
        CdRom_BindImage(disc,&archive);g_GameState.pe_image_base_lba=archive.sector;g_FxCommonNight=night;
        glyph_reads=glyph_polls=fx_read_failure=fx_poll_failure=0;
        memset(g_FxCommonTimTable,0xa5,FX_COMMON_RESOURCE_BYTES);memset(g_FxCommonModelTable,0xa5,FX_COMMON_RESOURCE_BYTES);
        if(FxCommon_LoadEffectSectors()!=0 || glyph_reads!=2 || glyph_polls!=2){valid=0;break;}
        for(unsigned bank=0;bank<2;++bank){unsigned index=bank?(night?1:2):0;
            size_t size=(sectors[index+1]-sectors[index])*2048u;
            const u8 *actual=(const u8 *)(bank?g_FxCommonModelTable:g_FxCommonTimTable);
            if(!pe_disc_read(disc,&archive,(uint64_t)sectors[index]*2048,expected,size) || memcmp(actual,expected,size)){valid=0;break;}
            for(size_t i=size;i<FX_COMMON_RESOURCE_BYTES;++i)if(actual[i]!=0xa5){valid=0;break;}
        }
    }
    if(valid){
        /* All 80 authored curves from the actual texture resource bank. */
        const u8 *bank=(const u8 *)g_FxCommonTimTable;u32 start,end;memcpy(&start,bank+8,4);memcpy(&end,bank+12,4);
        if(start>=end || end>41*2048u){valid=0;goto done;}
        const u8 *paths=bank+start;u32 first;memcpy(&first,paths,4);
        if(first!=80*4u){valid=0;goto done;}
        for(unsigned index=0;index<80 && valid;++index){u32 offset;s16 count;
            memcpy(&offset,paths+index*4,4);
            if(offset>end-start-8){valid=0;break;}memcpy(&count,paths+offset+6,2);
            if(count<=2 || offset+8+(u32)count*8>end-start){valid=0;break;}
            unsigned span=count-2;const unsigned fractions[]={0,1,127,255};
            for(unsigned seg=0;seg<span+3;++seg)for(unsigned f=0;f<4;++f)for(unsigned initial=0;initial<2;++initial){
                unsigned segment=seg==span+2?0xffffff:seg;FxCommonVec3 out;GteShortVector angles;
                memset(&out,0xa5,sizeof(out));memset(&angles,0xa5,sizeof(angles));g_FxCommonPathWrapped=initial;
                s32 result=FxCommon_SamplePath((segment<<8)|fractions[f],index,(void *)paths,&out,&angles);
                const void *parts[]={&out,&angles,&g_FxCommonPathWrapped,&result};unsigned sizes[]={12,8,1,4};
                for(unsigned part=0;part<4;++part)for(unsigned byte=0;byte<sizes[part];++byte)
                    hash=(hash^((const u8 *)parts[part])[byte])*UINT64_C(1099511628211);
                ++cases;
            }
        }
        if(cases!=80784 || hash!=UINT64_C(0x8fe143d65b03392d))valid=0;
    }
    for(unsigned fault=1;fault<=4;++fault){
        CdRom_BindImage(disc,&archive);glyph_reads=glyph_polls=0;
        fx_read_failure=fault==1?1:fault==3?2:0;fx_poll_failure=fault==2?1:fault==4?2:0;
        if(FxCommon_LoadEffectSectors()!=-(int)fault)valid=0;
    }
    fx_read_failure=fx_poll_failure=0;
    for(unsigned fault=0;fault<3;++fault){
        memcpy(g_PeImageSectorTable+76,sectors,sizeof(sectors));g_GameState.pe_image_base_lba=archive.sector;
        if(fault==0)g_PeImageSectorTable[77]=sectors[0];
        if(fault==1)g_PeImageSectorTable[77]=sectors[0]+129;
        if(fault==2)g_GameState.pe_image_base_lba=UINT32_MAX;
        glyph_reads=glyph_polls=0;
        if(FxCommon_LoadEffectSectors()!=-5 || glyph_reads || glyph_polls)valid=0;
    }
done:
    fx_image_io=0;fx_read_failure=fx_poll_failure=0;
    CdRom_BindImage(old_image,old_image?&old_archive:NULL);g_GameState=saved;g_FxCommonNight=old_night;
    memcpy(g_PeImageSectorTable+76,saved_sectors,sizeof(saved_sectors));free(expected);
    if(!valid){fprintf(stderr,"Original FX resources: cases=%u hash=%016llx\n",cases,(unsigned long long)hash);return 0;}
    puts("Original FX resources: CUE/BIN day/night banks match every byte with intact buffer tails; four I/O failures and invalid extents propagate; all 80 paths match 80784 unhooked USA samples");return 1;
}

static int original_positional_audio_projection(void)
{
    Pe1GameState state=g_GameState;GeomScrollRecord scroll=g_GeomScroll;
    GteMatrix views[3]={{{{4096,0,0},{0,4096,0},{0,0,4096}}},
        {{{0,0,4096},{0,4096,0},{-4096,0,0}}},{{{4096,0,0},{0,4096,0},{0,0,4096}}}};
    views[2].t[0]=23;views[2].t[1]=-17;views[2].t[2]=250;
    static const s16 positions[9][3]={{0,0,0},{0,0,1},{0,0,6000},{-1000,-500,2000},{1000,500,2000},
        {-32768,32767,32767},{32767,-32768,-32768},{-1,-1,-1},{200,300,30000}};
    static const s16 offsets[3][2]={{160,112},{-512,256},{0,0}};
    static const u16 ranges[4][2]={{0,10000},{128,2048},{2048,4096},{0,65535}};
    static const u8 limits[3][2]={{0,127},{48,127},{127,0}};
    /* Exactly six addressable bytes: the GTE must receive a padded local copy. */
    AkaoPackedRect3 *position=malloc(sizeof(*position));if(!position)return 0;
    uint64_t hash=UINT64_C(14695981039346656037);unsigned cases=0;int valid=1;
    for(unsigned view=0;view<3;++view)for(int projection=160;projection<=640;projection*=2)
    for(unsigned point=0;point<9;++point)for(unsigned offset=0;offset<3;++offset)
    for(unsigned extent=0;extent<4;++extent)for(unsigned limit=0;limit<3;++limit){
        for(unsigned i=0;i<32;++i){Psyz_GteDataWrite(i,0);Psyz_GteCtrlWrite(i,0);}
        g_ViewMatrixSlot.value=&views[view];g_GeomScroll.state.position.projectionDistance=&projection;
        g_GeomScroll.state.position.screenOffsetX=offsets[offset][0];g_GeomScroll.state.position.screenOffsetY=offsets[offset][1];
        g_GameState.bank_value_f8=ranges[extent][0];g_GameState.bank_value_fa=ranges[extent][1];
        g_GameState.bank_value_f6=limits[limit][0];g_GameState.bank_value_f7=limits[limit][1];
        position->x=positions[point][0];position->y=positions[point][1];position->z=positions[point][2];
        int pan=-1,volume=-1,result=Akao_Calc3DPan(position,&pan,&volume);
        u32 values[]={(u32)result,(u32)pan,(u32)volume,Psyz_GteCtrlRead(24),Psyz_GteCtrlRead(25)};
        for(unsigned i=0;i<5;++i)for(unsigned byte=0;byte<4;++byte)
            hash=(hash^(u8)(values[i]>>(byte*8)))*UINT64_C(1099511628211);
        if(pan<0 || pan>255 || volume<0 || volume>127)valid=0;++cases;
    }
    free(position);g_GameState=state;g_GeomScroll=scroll;
    if(!valid || hash!=UINT64_C(0xdd3f0ddcc154e8eb)){
        fprintf(stderr,"Original positional audio: cases=%u hash=%016llx valid=%d\n",cases,(unsigned long long)hash,valid);return 0;}
    puts("Original positional audio: 2916 USA CPU/GTE-bridge cases, views/projection/clipping, negative pan, distance-volume wrapping and restored screen offsets match");return 1;
}

static int original_glint_lifecycle(void)
{
    Pe1GameState saved_state=g_GameState;GeomScrollRecord saved_scroll=g_GeomScroll;
    RenderEffectParameters saved_params=g_FieldFxSpriteParams;
    FieldFxObjectPrefix *saved_owner=g_FieldFxOwner;FieldFxTaskSlot *saved_task=g_FieldFxActiveTaskSlot;
    FieldFxTaskContext context={0},*saved_context=g_FieldFxTaskContext;g_FieldFxTaskContext=&context;
    BattleEntity *saved_entity=D_8009D254;GteShortVector saved_origin=g_MemcardGlintOrigin;
    u8 *saved_model=g_FieldFxGlintModel;void *saved_sound=g_AkaoBgmHandle[0];
    int saved_slot=g_ActiveDrawSlot,saved_offset=g_DrawPacketOffset,saved_age=g_FieldFxCurrentAge;
    int saved_alt=g_FieldFxAltPaletteFlag; s16 saved_page=g_FieldFxLoadedTexturePage;
    u16 saved_stop=g_BattleCrossFlashStop;s16 saved_raised=g_MenuCardCrossFlashRaised;
    u32 saved_colors[256];memcpy(saved_colors,g_FieldModelColorBackup,sizeof(saved_colors));
    u16 model_page=g_FieldFxModelTpage,model_clut=g_FieldFxModelClut,model_semi=g_FieldFxModelSemiTrans;
    s16 model_depth=g_FieldFxModelDepthBias;
    u8 tracks[168];memcpy(tracks,g_MenuCardDriftGlowColorTrack,40);memcpy(tracks+40,g_MenuCardRisingEmberColorTrack,48);
    memcpy(tracks+88,g_MenuCardGlintBurstColorTrack,40);memcpy(tracks+128,g_MenuCardSpinRingColorTrack,40);
    _Alignas(16) static u8 packets[0x40000+128];RenderNativeTag ordering[4096];
    _Alignas(void *) u8 pool[4096];FieldFxTaskSlot task={.end=(char *)pool};
    FieldFxObjectPrefix owner={0};BattleEntity actor={0};RenderMatrix matrices[27]={0};
    for(unsigned i=0;i<27;++i){matrices[i].rotation[0][0]=matrices[i].rotation[1][1]=matrices[i].rotation[2][2]=4096;
        matrices[i].translation[0]=(int)i*10-100;matrices[i].translation[2]=6000;}
    actor.renderObject.matrices=matrices;actor.renderObject.target_z=6000;owner.actor=(FieldActor *)&actor;
    g_FieldFxOwner=&owner;g_FieldFxActiveTaskSlot=&task;g_PlayerEntity=(FieldActor *)&actor;
    GteMatrix view={.m={{4096,0,0},{0,4096,0},{0,0,4096}}};int projection=320;
    g_ViewMatrixSlot.value=&view;g_GeomScroll.state.position.projectionDistance=&projection;
    g_GameState.bank_value_f8=0;g_GameState.bank_value_fa=10000;g_GameState.bank_value_f6=0;g_GameState.bank_value_f7=127;
    u32 sound[32]={0};g_AkaoBgmHandle[0]=sound;
    _Alignas(16) u8 model_bytes[64]={0};FieldTexturedModel *model=(void *)model_bytes;
    model->header.flatTris=1;model->header.colorOffset=52;model->header.colorCount=3;
    for(unsigned i=0;i<3;++i){model->header.tris[0].vertex[i]=28+i*8;model->header.tris[0].uv[i]=0x1000+i;
        FieldModelVertex *v=(void *)(model_bytes+28+i*8);v->x=i==0?-20:i==1?20:0;v->y=i==2?20:-20;v->color=52+i*4;
        u32 color=0x00302010+i;memcpy(model_bytes+52+i*4,&color,4);}
    g_FieldFxGlintModel=model_bytes;
    FieldFxCallbackListCallback controllers[]={MenuCard_RingBurstController,MenuCard_CrossFlashController,
        MenuCard_GlintBurstController,MenuCard_SpinBurstController};
    const int counts[]={50,36,22,40},ends[]={72,48,8,8};
    unsigned draws=0;int valid=1;
    for(unsigned controller=0;controller<4;++controller)for(unsigned slot=0;slot<2;++slot)for(unsigned alt=0;alt<2;++alt){
        union {MemcardRingBurst ring;MemcardCrossFlash cross;MemcardGlintBurst glint;MemcardSpinBurst spin;} state={0};
        memset(pool,0xa5,sizeof(pool));g_ActiveDrawSlot=slot;g_FieldFxAltPaletteFlag=alt;g_FieldFxLoadedTexturePage=1;
        g_FieldFxSpriteParams=(RenderEffectParameters){32,2,1,0,0,0,16,32,32};g_FieldFxCurrentAge=0;
        g_BattleCrossFlashStop=55;g_MenuCardCrossFlashRaised=0;
        int used=controllers[controller](0,&state,0);FieldFxCallbackList *list=(void *)pool;
        if(used<=0 || used>sizeof(pool) || list->count!=counts[controller]){valid=0;break;}
        for(unsigned i=used;i<sizeof(pool);++i)if(pool[i]!=0xa5)valid=0;
        if(controller==1 && (state.cross.attachA!=6 || state.cross.attachB!=19 || g_BattleCrossFlashStop))valid=0;
        int ended=0;unsigned peak=0;
        for(int frame=0;frame<=90;++frame){
            g_FieldFxCurrentAge=frame;g_DrawPacketOffset=0;memset(packets,0xa5,sizeof(packets));
            for(unsigned i=0;i<4096;++i)ordering[i]=(RenderNativeTag){.address=0xffffff};
            D_800B0E38.ordering[slot]=ordering;D_800B0E38.packets[slot]=(char *)packets;
            Psyz_GteCtrlWrite(26,320);Psyz_GteCtrlWrite(29,1365);Psyz_GteCtrlWrite(30,1024);gte_SetGeomOffset(160,120);
            if(frame && !ended){if(controller==1 && frame==48)g_BattleCrossFlashStop=1;
                int result=controllers[controller](1,&state,0);
                if(result){ended=frame;if(result!=(controller==0?1:2))valid=0;}}
            if(!ended || frame==ended){controllers[controller](2,&state,0);++draws;}
            int active=FieldFx_DrawCallbackList((char *)pool);if(active>peak)peak=active;
            if(g_FieldFxCurrentAge!=frame || g_DrawPacketOffset<0 || g_DrawPacketOffset>0x40000){valid=0;break;}
            /* The original model renderer writes tentative UVs before culling.
             * Its final scratch packet need not advance the committed cursor. */
            for(unsigned i=g_DrawPacketOffset+sizeof(FieldModelGt4Packet);i<sizeof(packets);++i)
                if(packets[i]!=0xa5){valid=0;break;}
            for(unsigned i=0;i<4096;++i){uintptr_t link=ordering[i].address;unsigned count=0;
                while(link!=0xffffff){
                    if(link<(uintptr_t)packets || link>(uintptr_t)packets+g_DrawPacketOffset-sizeof(RenderNativeTag) || ++count>4096){valid=0;break;}
                    RenderNativeTag *tag=(void *)link;
                    if(tag->length>16 || link+sizeof(*tag)+tag->length*4>(uintptr_t)packets+g_DrawPacketOffset){valid=0;break;}
                    link=tag->address;
                }
            }
            FieldFx_UpdateCallbackList(pool);
            if(frame==90 && active)valid=0;
        }
        if(ended!=ends[controller] || !peak)valid=0;
        if(!valid)fprintf(stderr,"glint lifecycle controller=%u slot=%u alt=%u end=%d peak=%u offset=%d\n",controller,slot,alt,ended,peak,g_DrawPacketOffset);
        if(controller==1 && g_MenuCardCrossFlashRaised!=1)valid=0;
    }
    {
    PmTables saved_pm=g_PmTables;u32 saved_flags=g_GameStateFlags;
    void *saved_programs[104];memcpy(saved_programs,g_FieldFxTaskPrograms,sizeof(saved_programs));
    static PmPrimarySlot bank[11];
    g_PmTables=(PmTables){g_PmCmdHandlerArray,bank,NULL};g_GameStateFlags=0x80;
    g_GameState.flags&=~8u;g_GameState.tim_load_state=0;
    const unsigned masks[]={1,1,3,7,7,7,1};unsigned programs_run=0,combined_frames=0;
    for(unsigned command=108;command<=114;++command){
        memset(bank,0,sizeof(bank));
        for(unsigned i=0;i<11;++i)memset(bank[i].task.tasks.arena,0xa5,sizeof(bank[i].task.tasks.arena));
        g_GameState.flags&=~0x10000u;g_FieldFxAltPaletteFlag=0;g_FieldFxLoadedTexturePage=1;
        g_FieldFxSpriteParams=(RenderEffectParameters){32,2,1,0,0,0,16,32,32};
        if(Scene_LoadRoomAssets(command,&actor)!=0 || Pm_SendCmd(0,0,0,0,0,0)){valid=0;break;}
        if(command==114)g_MenuCardCrossFlashRaised=1;
        unsigned seen=0,max_used=0,finished=0;int companion=-1;
        for(unsigned frame=0;frame<150;++frame){
            g_ActiveDrawSlot=frame&1;g_DrawPacketOffset=0;memset(packets,0xa5,sizeof(packets));
            for(unsigned i=0;i<4096;++i)ordering[i]=(RenderNativeTag){.address=0xffffff};
            D_800B0E38.ordering[g_ActiveDrawSlot]=ordering;D_800B0E38.packets[g_ActiveDrawSlot]=(char *)packets;
            Psyz_GteCtrlWrite(26,320);Psyz_GteCtrlWrite(29,1365);Psyz_GteCtrlWrite(30,1024);gte_SetGeomOffset(160,120);
            if(command==109 && frame==4){
                companion=Scene_LoadRoomAssets(114,&actor);
                if(companion!=1 || Pm_SendCmd(companion,0,0,0,0,0))valid=0;
            }
            if(command==109 && frame==48)g_BattleCrossFlashStop=1;
            if(Pm_Start(0) || (companion>=0 && Pm_Start(companion)) || Pm_Exec(0) ||
               (companion>=0 && Pm_Exec(companion))){valid=0;break;}
            FieldFxTaskContext *ctx=&bank[0].task.tasks;
            if(ctx->used>sizeof(ctx->arena) || g_DrawPacketOffset<0 || g_DrawPacketOffset>0x40000){valid=0;break;}
            if(ctx->used>max_used)max_used=ctx->used;
            for(unsigned i=0;i<8;++i)if(ctx->slots[i].id!=0xffff){
                FieldFxTaskSlot *slot=&ctx->slots[i];if(slot->id>2){valid=0;break;}
                seen|=1u<<slot->id;
                if(slot->start<ctx->arena || slot->start>=ctx->arena+sizeof(ctx->arena) ||
                    (slot->end && (slot->end<slot->start || slot->end>=ctx->arena+sizeof(ctx->arena))))valid=0;
            }
            for(unsigned i=max_used;i<sizeof(ctx->arena);++i)if((u8)ctx->arena[i]!=0xa5){valid=0;break;}
            for(unsigned i=g_DrawPacketOffset+sizeof(FieldModelGt4Packet);i<sizeof(packets);++i)
                if(packets[i]!=0xa5){valid=0;break;}
            for(unsigned i=0;i<4096;++i){uintptr_t link=ordering[i].address;unsigned count=0;
                while(link!=0xffffff){
                    if(link<(uintptr_t)packets || link>(uintptr_t)packets+g_DrawPacketOffset-sizeof(RenderNativeTag) || ++count>4096){valid=0;break;}
                    RenderNativeTag *tag=(void *)link;
                    if(tag->length>16 || link+sizeof(*tag)+tag->length*4>(uintptr_t)packets+g_DrawPacketOffset){valid=0;break;}
                    link=tag->address;
                }
            }
            if(command==109 && frame>=4 && frame<48){
                if(bank[1].header.state!=2 || !bank[1].task.tasks.count)valid=0;
                ++combined_frames;
            }
            if(!bank[0].header.state && (companion<0 || !bank[companion].header.state)){finished=frame;break;}
        }
        if(!finished || bank[0].task.tasks.count || seen!=masks[command-108] ||
           (command>=111 && command<=113 && max_used<=0x97c))valid=0;
        if(command==109 || command==114){
            if(g_GameState.flags&0x10000)valid=0;
            for(unsigned i=23;i<30;++i)if(g_FieldFxTaskPrograms[i])valid=0;
        }
        if(!valid)fprintf(stderr,"native task program %u: seen=%x arena=%u finish=%u count=%u state=%u\n",command,seen,max_used,finished,bank[0].task.tasks.count,bank[0].header.state);
        ++programs_run;
    }
    if(programs_run!=7 || combined_frames!=44)valid=0;
    memcpy(g_FieldFxTaskPrograms,saved_programs,sizeof(saved_programs));g_PmTables=saved_pm;g_GameStateFlags=saved_flags;
    }
    memcpy(g_MenuCardDriftGlowColorTrack,tracks,40);memcpy(g_MenuCardRisingEmberColorTrack,tracks+40,48);
    memcpy(g_MenuCardGlintBurstColorTrack,tracks+88,40);memcpy(g_MenuCardSpinRingColorTrack,tracks+128,40);
    memcpy(g_FieldModelColorBackup,saved_colors,sizeof(saved_colors));g_FieldFxGlintModel=saved_model;g_AkaoBgmHandle[0]=saved_sound;
    g_FieldFxModelTpage=model_page;g_FieldFxModelClut=model_clut;g_FieldFxModelSemiTrans=model_semi;g_FieldFxModelDepthBias=model_depth;
    g_GameState=saved_state;g_GeomScroll=saved_scroll;g_FieldFxSpriteParams=saved_params;
    g_FieldFxOwner=saved_owner;g_FieldFxActiveTaskSlot=saved_task;g_FieldFxTaskContext=saved_context;g_PlayerEntity=(FieldActor *)saved_entity;g_MemcardGlintOrigin=saved_origin;
    g_ActiveDrawSlot=saved_slot;g_DrawPacketOffset=saved_offset;g_FieldFxCurrentAge=saved_age;
    g_FieldFxAltPaletteFlag=saved_alt;g_FieldFxLoadedTexturePage=saved_page;g_BattleCrossFlashStop=saved_stop;g_MenuCardCrossFlashRaised=saved_raised;
    if(!valid){fprintf(stderr,"original glint controller/list/render lifecycle failed (%u draws)\n",draws);return 0;}
    puts("Original glint controllers/programs: all four controllers and seven real PM programs, overlapping tasks, cross-flash/114 handshake, complete lifetimes, both draw slots and native arena/OT guards passed");
    return 1;
}

static int original_task_bank_data(PeDisc *disc)
{
    PeDiscFile archive;static u8 bank[9*2048];
    if(!pe_disc_find(disc,"PE.IMG",&archive) || !pe_disc_read(disc,&archive,969ull*2048,bank,sizeof(bank)))return 0;
    FieldFxTaskProgram *programs[]={&g_FieldFxTaskProgramCmd108,&g_FieldFxTaskProgramCmd109,
        &g_FieldFxTaskProgramCmd110,&g_FieldFxTaskProgramCmd111,&g_FieldFxTaskProgramCmd112,
        &g_FieldFxTaskProgramCmd113,&g_FieldFxTaskProgramCmd114};
    const u32 addresses[]={0x801f1bd8,0x801f1c58,0x801f1d00,0x801f1d8c,0x801f1e18,0x801f1ea4,0x801f1ef0};
    static const struct {FieldFxCallbackListCallback callback;u32 address;} callbacks[]={
        {MenuCard_RingBurstController,0x801edc44},{MenuCard_CrossFlashController,0x801eebc8},
        {MenuCard_GlintBurstController,0x801ef978},{MenuCard_SpinBurstController,0x801f0690},
        {MenuCard_LinkEffect,0x801f0d30},{MenuCard_TrailEffect,0x801f11fc},
        {MenuCard_SineTrailEffect,0x801f1790},{MenuCard_ConsumeCrossFlashFlag,0x801f1b60}};
    intptr_t (*initializers[])(int,intptr_t,intptr_t,intptr_t)={MenuCard_GetGlintOriginA,MenuCard_GetGlintOriginB,
        MenuCard_GetGlintOriginC,MenuCard_GetGlintOriginD,MenuCard_GetGlintOriginE,MenuCard_GetGlintOriginF};
    const unsigned words[]={12,12,15,18,18,18,9};int valid=1;
    for(unsigned i=0;i<7;++i){
        const u8 *record=bank+(addresses[i]-0x801ed7f8u);FieldFxTaskProgram *program=programs[i];
        for(unsigned j=0;j<8;++j){u32 address=0,original;u16 size;
            if(program->table.callbacks[j]){for(unsigned k=0;k<8;++k)
                if(program->table.callbacks[j]==callbacks[k].callback)address=callbacks[k].address;
                if(!address)valid=0;}
            memcpy(&original,record+j*4,4);memcpy(&size,record+32+j*2,2);
            if(original!=address || size!=program->table.sizes[j])valid=0;
        }
        u32 initializer,script;memcpy(&initializer,record+48,4);memcpy(&script,record+52,4);
        if(initializer!=(i<6?0x801f1b00u+i*16:0) || program->initialize!=(i<6?initializers[i]:NULL))valid=0;
        if(program->initialize && program->initialize(-1,INTPTR_MAX,INTPTR_MIN,0)!=(intptr_t)&g_MemcardGlintOrigin)valid=0;
        if(script<0x801ed7f8u || script-0x801ed7f8u+words[i]*2>sizeof(bank) ||
            memcmp(program->script,bank+(script-0x801ed7f8u),words[i]*2))valid=0;
    }
    static const struct {const void *data;u32 address;size_t bytes;} authored[]={
        {&g_MenuCardLinkColor1,0x801ed850,4},{&g_MenuCardLinkColor2,0x801ed854,4},
        {g_MenuCardPulseColorTrack,0x801f1d5c,40},{g_MenuCardTrailColorTrack,0x801f1de8,48},
        {g_MenuCardSineTrailColorTrack,0x801f1e74,48},{&g_MenuCardLinkJointA,0x801f1d86,2},
        {&g_MenuCardLinkJointB,0x801f1d88,2}};
    for(unsigned i=0;i<sizeof(authored)/sizeof(*authored);++i)
        if(memcmp(authored[i].data,bank+(authored[i].address-0x801ed7f8u),authored[i].bytes))valid=0;
    FieldFxCallbackListCallback particles[]={MenuCard_PulseImage,MenuCard_CosineImage,MenuCard_SineRotatingImage};
    static const int timers[]={0,1,10,11,12,14,15,16,17,22,23,24,31,39,43,32767,32768,65535};
    static const s16 phases[]={-32768,-1,0,1,32767};static const int modes[]={-1,0,1,3,255};
    uint64_t hash=UINT64_C(14695981039346656037);unsigned cases=0;
    for(unsigned fn=0;fn<3;++fn)for(unsigned phase=0;phase<5;++phase)
    for(unsigned timer=0;timer<18;++timer)for(unsigned mode=0;mode<5;++mode){
        u16 words[10]={32767,32768,(u16)-10,0xa55a,(u16)-100,200,(u16)-300,0x5aa5,(u16)phases[phase],(u16)timers[timer]};
        MemcardPulseState particle;memcpy(&particle,words,20);
        int result=particles[fn](modes[mode],&particle,INTPTR_MAX);const u8 *bytes=(const u8 *)&particle;
        for(unsigned i=0;i<20;++i)hash=(hash^bytes[i])*UINT64_C(1099511628211);
        for(unsigned i=0;i<4;++i)hash=(hash^(u8)((u32)result>>(8*i)))*UINT64_C(1099511628211);
        ++cases;
    }
    if(cases!=1350 || hash!=UINT64_C(0x920c3087dc472d02)){
        fprintf(stderr,"Original image particles: cases=%u hash=%016llx\n",cases,(unsigned long long)hash);valid=0;}
    if(!valid)fprintf(stderr,"original native task descriptors/scripts/data differ from supplied USA bank\n");
    else puts("Original task bank: all seven descriptors, 56 callbacks/sizes, six initializer returns, 102 script halfwords and image data match USA; 1350 image-particle CPU cases match");
    return valid;
}

static int original_glint_particles(PeDisc *disc)
{
    static const int timers[]={0,1,14,15,16,17,22,23,24,30,31,38,39,43,44,32767,32768,65535};
    static const s16 poses[6][6]={{0,0,0,0,0,0},{-500,499,300,70,-80,33},
        {32767,-32768,32760,-32768,32767,-1},{-32768,32767,-32760,32767,-32768,1},
        {35,0,-35,-1,1,127},{-1,-1,-1,-127,-128,128}};
    FieldFxCallbackListCallback functions[]={MenuCard_DriftGlowParticle,MenuCard_RisingEmberParticle,
        MenuCard_FadingGlintParticle,MenuCard_SpinRingParticle};
    static const u32 seeds[]={1,0x87654321};static const int modes[]={0,1,3};
    RoomFloorY floor=*g_RoomFloorY;unsigned saved_random=Psyz_RandState();
    uint64_t hash=UINT64_C(14695981039346656037);unsigned cases=0;int valid=1;
#define GLINT_PUT(value,bytes) do {uint64_t v_=(uint64_t)(value); \
    for(unsigned b_=0;b_<(bytes);++b_){hash=(hash^(u8)v_)*UINT64_C(1099511628211);v_>>=8;}}while(0)
    for(unsigned fn=0;fn<4;++fn)for(int state=-1;state<4;++state)
    for(unsigned timer=0;timer<sizeof(timers)/sizeof(*timers);++timer)for(unsigned pose=0;pose<6;++pose)
    for(int height=-500;height<=500;height+=500)for(unsigned seed=0;seed<2;++seed)for(unsigned mode=0;mode<3;++mode){
        u16 words[10]={(u16)poses[pose][0],(u16)poses[pose][1],(u16)poses[pose][2],0xa55a,
            (u16)poses[pose][3],(u16)poses[pose][4],(u16)poses[pose][5],0x5aa5,(u16)state,(u16)timers[timer]};
        union {RoomDampedSpark spark;MemcardSpinParticle spin;} particle;
        memcpy(&particle,words,20);g_RoomFloorY->y=height;srand(seeds[seed]);
        int result=functions[fn](modes[mode],&particle,0);
        const u8 *bytes=(const u8 *)&particle;for(unsigned i=0;i<20;++i)GLINT_PUT(bytes[i],1);
        GLINT_PUT(result,4);
        /* Match the complete RNG state, including the three jitter draws. */
        u32 rng=seeds[seed];if(fn==1 && state==1 && modes[mode]==1)
            for(unsigned i=0;i<3;++i)rng=rng*UINT32_C(1103515245)+12345;
        GLINT_PUT(rng,4);
        u32 next=rng*UINT32_C(1103515245)+12345;if(rand()!=((next>>16)&32767))valid=0;
        ++cases;
    }
    *g_RoomFloorY=floor;srand(saved_random);
#undef GLINT_PUT
    if(cases!=38880 || hash!=UINT64_C(0x445fa6d24cc84f79))valid=0;
    PeDiscFile archive;static u8 bank[22*2048];
    if(!pe_disc_find(disc,"PE.IMG",&archive) || !pe_disc_read(disc,&archive,965ull*2048,bank,sizeof(bank)))return 0;
    static const struct {const void *data;u32 address;size_t bytes;} authored[]={
        {&g_MenuCardEffectSpinRotation,0x801ed7fc,8},{&g_MenuCardEffectTiltRotation,0x801ed804,8},
        {&g_MenuCardRingBurstGlowColor,0x801ed80c,4},{&g_MenuCardRingBurstRingColor,0x801ed810,4},
        {&g_MenuCardRingBurstBandColor,0x801ed814,4},{&g_MenuCardHandOffset,0x801ed818,8},
        {&g_MenuCardCrossFlashTipAOffset,0x801ed820,8},{&g_MenuCardCrossFlashTipBOffset,0x801ed828,8},
        {&g_MenuCardCrossFlashCenterOffset,0x801ed830,8},{&g_MenuCardCrossFlashInnerColor,0x801ed838,4},
        {&g_MenuCardCrossFlashOuterColor,0x801ed83c,4},{&g_MenuCardCrossFlashEdgeColor,0x801ed840,4},
        {&g_MenuCardGlintBurstFlashColor,0x801ed844,4},{&g_MenuCardSpinRingColorA,0x801ed848,4},
        {&g_MenuCardSpinRingColorB,0x801ed84c,4},{g_MenuCardDriftGlowColorTrack,0x801f1bb0,40},
        {g_MenuCardRisingEmberColorTrack,0x801f1c28,48},{g_MenuCardGlintBurstColorTrack,0x801f1cb0,40},
        {g_MenuCardSpinRingColorTrack,0x801f1cd8,40}};
    u16 state_bytes;memcpy(&state_bytes,bank+(0x801f1c78u-0x801eb7f8u),2);
    if(state_bytes!=sizeof(MemcardCrossFlash))valid=0;
    for(unsigned i=0;i<sizeof(authored)/sizeof(*authored);++i)
        if(memcmp(authored[i].data,bank+(authored[i].address-0x801eb7f8),authored[i].bytes))valid=0;
    if(!valid){fprintf(stderr,"Original glint particles: cases=%u hash=%016llx valid=%d\n",cases,(unsigned long long)hash,valid);return 0;}
    puts("Original glint particles: 38880 real USA cases, four callbacks, all phases, signed/wrapping coordinates and timers, damping/bounce, jitter RNG and authored data match");
    return 1;
}

static int original_script_sprites(void)
{
    Pe1GameState saved_state=g_GameState;GeomScrollRecord saved_scroll=g_GeomScroll;
    int saved_offset=g_DrawPacketOffset,saved_slot=g_ActiveDrawSlot;
    FieldScriptSprite *saved_list=g_FieldFxScriptSpriteList;
    s16 saved_count=g_FieldFxScriptSpriteCount;FieldScriptSprite saved_records[22]={0};
    if(saved_list)memcpy(saved_records,saved_list-21,sizeof(saved_records));
    FieldFx_ClearScriptSprites();FieldScriptSprite *list=g_FieldFxScriptSpriteList;
    GteMatrix view={0};view.m[0][0]=view.m[1][1]=view.m[2][2]=4096;
    int projection=320;g_ViewMatrixSlot.value=&view;g_GeomScroll.state.position.projectionDistance=&projection;
    union {uintptr_t alignment;u8 bytes[0x10000];} arena;
    RenderNativeTag ordering[4096];
    uint64_t hash=UINT64_C(14695981039346656037);unsigned cases=0;int valid=1;
#define SPRITE_PUT(value,bytes) do {uint64_t v_=(uint64_t)(value); \
    for(unsigned b_=0;b_<(bytes);++b_){hash=(hash^(u8)v_)*UINT64_C(1099511628211);v_>>=8;}}while(0)
#define SPRITE_STATE() do {SPRITE_PUT(g_FieldFxScriptSpriteCount,2); \
    for(unsigned i_=0;i_<22;++i_){const u8 *p_=(const u8 *)&list[-(int)i_]; \
        for(unsigned j_=0;j_<20;++j_)SPRITE_PUT(p_[j_],1);}}while(0)
#define SPRITE_SETUP() do { \
    for(unsigned i_=0;i_<22;++i_){memset(&list[-(int)i_],0xA5,20);list[-(int)i_].active=0;} \
    g_FieldFxScriptSpriteCount=0;FieldFx_ClearScriptSprites(); \
    for(unsigned i_=0;i_<32;++i_){Psyz_GteDataWrite(i_,0);Psyz_GteCtrlWrite(i_,0);} \
    Psyz_GteCtrlWrite(24,160<<16);Psyz_GteCtrlWrite(25,112<<16);Psyz_GteCtrlWrite(26,320); \
}while(0)
#define SPRITE_STEP(slot) do { \
    memset(arena.bytes,0xA5,sizeof(arena.bytes)); \
    for(unsigned i_=0;i_<4096;++i_)ordering[i_]=(RenderNativeTag){0xFFFFFF,0xAB}; \
    g_DrawPacketOffset=0;g_ActiveDrawSlot=(slot); \
    g_RenderAnimBufferBase[0]=g_RenderAnimBufferBase[1]=arena.bytes; \
    D_800B0E38.ordering[0]=D_800B0E38.ordering[1]=ordering; \
    FieldFx_UpdateScriptSprites();SPRITE_STATE(); \
    if(g_DrawPacketOffset<0 || (size_t)g_DrawPacketOffset>sizeof(arena.bytes) || g_DrawPacketOffset%sizeof(RenderTexturedQuad)){valid=0;goto sprites_restore;} \
    unsigned count_=(unsigned)g_DrawPacketOffset/sizeof(RenderTexturedQuad);SPRITE_PUT(count_,2); \
    for(unsigned i_=0;i_<count_;++i_){RenderTexturedQuad *p_=(RenderTexturedQuad *)arena.bytes+i_; \
        if(p_->tag.length!=9)valid=0;const u8 *data_=(const u8 *)p_+sizeof(RenderNativeTag); \
        for(unsigned j_=0;j_<36;++j_)SPRITE_PUT(data_[j_],1);} \
    for(unsigned layer_=0;layer_<4096;++layer_){uintptr_t link_=ordering[layer_].address;unsigned links_=0; \
        if(ordering[layer_].length!=0xAB)valid=0;if(link_==0xFFFFFF)continue;SPRITE_PUT(layer_,2); \
        while(link_!=0xFFFFFF){ \
            if(link_<(uintptr_t)arena.bytes || link_>=(uintptr_t)(arena.bytes+g_DrawPacketOffset) || \
               (link_-(uintptr_t)arena.bytes)%sizeof(RenderTexturedQuad) || ++links_>22){valid=0;goto sprites_restore;} \
            SPRITE_PUT((link_-(uintptr_t)arena.bytes)/sizeof(RenderTexturedQuad),1);link_=((RenderNativeTag *)link_)->address;} \
        SPRITE_PUT(links_,1);}SPRITE_PUT(65535,2); \
    for(unsigned i_=g_DrawPacketOffset;i_<sizeof(arena.bytes);++i_)if(arena.bytes[i_]!=0xA5)valid=0; \
}while(0)
    const u32 masks[]={0,1,0x555555,0xAAAAAA,0xFFFFF,0x3FFFFF,1<<21};
    for(unsigned m=0;m<7;++m){
        SPRITE_SETUP();int count=0;
        for(unsigned i=0;i<22;++i){list[-(int)i].active=(masks[m]>>i)&1;count+=list[-(int)i].active;}
        g_FieldFxScriptSpriteCount=count>20?20:count;FieldFx_ClearScriptSprites();SPRITE_STATE();++cases;
    }
    const int delays[]={0,1,3,17,255},sizes[]={-32768,-1,0,1,32,32767},spawn_counts[]={1,4,20,21,22};
    for(unsigned mode=0;mode<3;++mode)for(unsigned delay=0;delay<5;++delay)for(unsigned size=0;size<6;++size)
    for(unsigned slot=0;slot<2;++slot)for(unsigned spawn=0;spawn<5;++spawn){
        SPRITE_SETUP();
        for(int i=0;i<spawn_counts[spawn];++i){s16 point[]={i*25-100,-i*7,1000+i*8};
            FieldFx_SpawnScriptSprite(point,mode,sizes[size],delays[delay],i*17+31,i*11+64,511-i*9);SPRITE_STATE();}
        for(unsigned tick=0;tick<8;++tick)SPRITE_STEP(slot);
        FieldFx_ClearScriptSprites();SPRITE_STATE();++cases;
    }
    const unsigned long_runs[][3]={{0,255,1030},{1,1,520},{1,3,200}};
    for(unsigned i=0;i<3;++i){
        SPRITE_SETUP();s16 point[]={-25,-100,1000};
        FieldFx_SpawnScriptSprite(point,long_runs[i][0],32,long_runs[i][1],200,128,64);SPRITE_STATE();
        for(unsigned tick=0;tick<long_runs[i][2];++tick)SPRITE_STEP(tick&1);
        ++cases;
    }

    /* Native arena exhaustion and projection guards do not alter pool timing. */
    for(unsigned remaining=0;remaining<=168;remaining+=8){
        SPRITE_SETUP();s16 point[]={0,0,1000};
        for(unsigned i=0;i<20;++i)FieldFx_SpawnScriptSprite(point,0,32,255,0,0,0);
        memset(arena.bytes,0xA5,sizeof(arena.bytes));
        for(unsigned i=0;i<4096;++i)ordering[i]=(RenderNativeTag){0xFFFFFF,0xAB};
        g_ActiveDrawSlot=0;g_RenderAnimBufferBase[0]=arena.bytes;D_800B0E38.ordering[0]=ordering;
        unsigned start=RENDER_ANIM_BUFFER_BYTES-remaining;g_DrawPacketOffset=start;
        FieldFx_UpdateScriptSprites();
        unsigned emitted=remaining/sizeof(RenderTexturedQuad);
        if(g_DrawPacketOffset!=(int)(start+emitted*sizeof(RenderTexturedQuad)) || g_FieldFxScriptSpriteCount!=20)valid=0;
        for(unsigned i=0;i<start;++i)if(arena.bytes[i]!=0xA5)valid=0;
        for(unsigned i=RENDER_ANIM_BUFFER_BYTES;i<sizeof(arena.bytes);++i)if(arena.bytes[i]!=0xA5)valid=0;
        unsigned linked=0;
        for(unsigned layer=0;layer<4096;++layer){uintptr_t link=ordering[layer].address;
            while(link!=0xFFFFFF){
                if(link<(uintptr_t)(arena.bytes+start) || link>=(uintptr_t)(arena.bytes+g_DrawPacketOffset) || ++linked>emitted){valid=0;break;}
                link=((RenderNativeTag *)link)->address;
            }
        }
        if(linked!=emitted)valid=0;
    }
    for(unsigned invalid=0;invalid<3;++invalid){
        SPRITE_SETUP();s16 point[]={0,0,invalid==2?20000:0};
        projection=invalid==0?0:320;
        FieldFx_SpawnScriptSprite(point,0,32,1,0,0,0);
        memset(arena.bytes,0xA5,sizeof(arena.bytes));
        for(unsigned i=0;i<4096;++i)ordering[i]=(RenderNativeTag){0xFFFFFF,0xAB};
        g_DrawPacketOffset=0;g_ActiveDrawSlot=0;
        FieldFx_UpdateScriptSprites();
        for(unsigned i=0;i<4096;++i)if(ordering[i].address!=0xFFFFFF || ordering[i].length!=0xAB)valid=0;
        for(unsigned i=sizeof(RenderTexturedQuad);i<sizeof(arena.bytes);++i)if(arena.bytes[i]!=0xA5)valid=0;
    }
sprites_restore:
#undef SPRITE_STEP
#undef SPRITE_SETUP
#undef SPRITE_STATE
#undef SPRITE_PUT
    memcpy(list-21,saved_records,sizeof(saved_records));g_FieldFxScriptSpriteList=saved_list;g_FieldFxScriptSpriteCount=saved_count;
    g_GameState=saved_state;g_GeomScroll=saved_scroll;g_DrawPacketOffset=saved_offset;g_ActiveDrawSlot=saved_slot;
    if(!valid || hash!=UINT64_C(0x1c171246cd9ca458)){
        fprintf(stderr,"Original script sprites: cases=%u hash=%016llx valid=%d\n",cases,(unsigned long long)hash,valid);return 0;}
    puts("Original script sprites: 910 USA CPU/GTE-bridge scenarios, pool/clear/reuse, two modes, timers, RGB fades, long lifecycles and FT4/OT payloads match");return 1;
}

static int original_wireframe_renderer(void)
{
    GteMatrix view;
    GteMatrix *saved_view=g_ViewMatrixSlot.value;
    RenderNativeTag ordering[4096];
    union {uintptr_t alignment;u8 bytes[0x4000];} arena;
    const u32 scales[]={0,0x400000,0xC80000,0xFFFF0000};
    const int yaws[]={0,256,1024,2048,3072,4095};
    const unsigned materials[]={0,32,33,34,35};
    const s16 rotations[][9]={{4096,0,0,0,4096,0,0,0,4096},
        {0,0,4096,0,4096,0,-4096,0,0},{4096,0,0,0,4096,0,0,0,4096}};
    u32 saved_xy[256];s16 saved_depth[256];
    memcpy(saved_xy,g_WireScratch.screenXY,sizeof(saved_xy));memcpy(saved_depth,g_WireScratch.depth,sizeof(saved_depth));
    uint64_t hash=UINT64_C(14695981039346656037);int valid=1;unsigned samples=0;
#define WIRE_PUT(value,bytes) do {uint64_t v_=(uint64_t)(value); \
    for(unsigned b_=0;b_<(bytes);++b_){hash=(hash^(u8)v_)*UINT64_C(1099511628211);v_>>=8;}}while(0)
    for(unsigned orientation=0;orientation<2;++orientation)for(unsigned floor=0;floor<8;++floor)
    for(unsigned material=0;material<5;++material)for(unsigned scale=0;scale<4;++scale)
    for(unsigned yaw=0;yaw<6;++yaw)for(unsigned position=0;position<3;++position)for(unsigned camera=0;camera<3;++camera){
        for(unsigned i=0;i<32;++i){Psyz_GteDataWrite(i,0);Psyz_GteCtrlWrite(i,0);}
        Psyz_GteCtrlWrite(24,160<<16);Psyz_GteCtrlWrite(25,112<<16);Psyz_GteCtrlWrite(26,320);
        memset(&view,0,sizeof(view));memcpy(view.m,rotations[camera],sizeof(view.m));
        if(camera==2){view.t[0]=23;view.t[1]=-17;view.t[2]=250;}g_ViewMatrixSlot.value=&view;
        union {u32 alignment;u8 bytes[128];} frame={0};
        frame.bytes[1]=3;frame.bytes[6]=16;frame.bytes[10]=1;frame.bytes[19]=2;
        const s16 xs[]={-100,100,0},zs[]={0,0,100};
        for(unsigned i=0;i<3;++i){s16 xyz[]={xs[i],floor>>i&1?0:-100,zs[i]};
            memcpy(frame.bytes+28+i*8,xyz,sizeof(xyz));frame.bytes[28+i*8+6]=i%2;}
        memcpy(frame.bytes+52,orientation?(u8[]){1,0,2,0}:(u8[]){0,1,2,0},4);
        memcpy(frame.bytes+56,(u8[]){255,17,129,18},4);frame.bytes[67]=frame.bytes[71]=materials[material];
        RenderAnimPositionInput point={0};point.x=(s16[]){-50,0,50}[position];point.z=(s16[]){1000,8000,15000}[position];
        memset(arena.bytes,0xA5,sizeof(arena.bytes));
        memset(g_WireScratch.screenXY,0xA5,sizeof(saved_xy));memset(g_WireScratch.depth,0xA5,sizeof(saved_depth));
        for(unsigned i=0;i<4096;++i)ordering[i]=(RenderNativeTag){0xFFFFFF,0xAB};
        int used=Render_DrawWireframeModel((struct RenderAnimFrame *)frame.bytes,&point,(s32)scales[scale],yaws[yaw],ordering,arena.bytes,sizeof(arena.bytes));
        u8 *cur=arena.bytes;unsigned count=0,old_used=0;RenderNativeTag *packets[16];
        if(used<0 || used>(int)sizeof(arena.bytes)){valid=0;goto wire_restore;}
        while(cur<arena.bytes+used){
            RenderNativeTag *tag=(RenderNativeTag *)cur;
            if(count>=16 || (tag->length!=0 && tag->length!=1 && tag->length!=4)){valid=0;goto wire_restore;}
            packets[count++]=tag;old_used+=(tag->length+1)*4;
            cur+=tag->length==0?sizeof(RenderNativeTag):tag->length==1?sizeof(RenderDrawModePacket):sizeof(FieldLineG2Packet);
        }
        if(cur!=arena.bytes+used){valid=0;goto wire_restore;}
        WIRE_PUT(old_used,4);
        for(unsigned i=0;i<3;++i){WIRE_PUT(g_WireScratch.screenXY[i],4);WIRE_PUT(g_WireScratch.depth[i],2);}
        for(unsigned i=56;i<60;++i)WIRE_PUT(frame.bytes[i],1);
        for(unsigned i=0;i<count;++i){RenderNativeTag *tag=packets[i];WIRE_PUT(tag->length,1);
            const u8 *payload=(const u8 *)tag+sizeof(*tag);for(unsigned j=0;j<tag->length*4;++j)WIRE_PUT(payload[j],1);}
        for(unsigned layer=0;layer<4096;++layer){
            uintptr_t link=ordering[layer].address;unsigned links=0;
            if(ordering[layer].length!=0xAB)valid=0;
            if(link==0xFFFFFF)continue;WIRE_PUT(layer,2);
            while(link!=0xFFFFFF){
                unsigned id=0;while(id<count && (uintptr_t)packets[id]!=link)++id;
                if(id==count || ++links>=20){valid=0;goto wire_restore;}
                WIRE_PUT(id,1);link=packets[id]->address;
            }
            WIRE_PUT(links,1);
        }
        WIRE_PUT(65535,2);++samples;
    }

    /* Allocation pressure with the three-edge, three-draw-mode reference
     * case, plus invalid face indices and clipped depths. */
    {
        union {u32 alignment;u8 bytes[4096];} mesh={0};
        mesh.bytes[1]=3;mesh.bytes[6]=16;mesh.bytes[10]=1;
        const s16 positions[][3]={{-100,0,0},{100,-100,0},{0,-100,100}};
        for(unsigned i=0;i<3;++i)memcpy(mesh.bytes+28+i*8,positions[i],6);
        memcpy(mesh.bytes+52,(u8[]){0,1,2,0},4);mesh.bytes[67]=32;
        memset(&view,0,sizeof(view));view.m[0][0]=view.m[1][1]=view.m[2][2]=4096;
        RenderAnimPositionInput point={0};point.z=8000;
        for(unsigned capacity=0;capacity<=176;capacity+=8){
            for(unsigned i=0;i<32;++i){Psyz_GteDataWrite(i,0);Psyz_GteCtrlWrite(i,0);}
            Psyz_GteCtrlWrite(24,160<<16);Psyz_GteCtrlWrite(25,112<<16);Psyz_GteCtrlWrite(26,320);
            memset(arena.bytes,0xA5,sizeof(arena.bytes));
            for(unsigned i=0;i<4096;++i)ordering[i]=(RenderNativeTag){0xFFFFFF,0xAB};
            int used=Render_DrawWireframeModel((struct RenderAnimFrame *)mesh.bytes,&point,0x400000,2048,ordering,arena.bytes,capacity);
            if(used<0 || (unsigned)used>capacity || (capacity>=168 && used!=168))valid=0;
            for(unsigned i=capacity;i<sizeof(arena.bytes);++i)if(arena.bytes[i]!=0xA5)valid=0;
            for(unsigned i=0;i<4096;++i){uintptr_t link=ordering[i].address;unsigned links=0;
                if(ordering[i].length!=0xAB)valid=0;
                while(link!=0xFFFFFF){
                    if(link<(uintptr_t)arena.bytes || link+sizeof(RenderNativeTag)>(uintptr_t)(arena.bytes+used) || ++links>6){valid=0;break;}
                    RenderNativeTag *tag=(RenderNativeTag *)link;
                    if((tag->length!=1 && tag->length!=4) || link+sizeof(*tag)+tag->length*4>(uintptr_t)(arena.bytes+used)){valid=0;break;}
                    link=tag->address;
                }
            }
        }
        mesh.bytes[52]=3;
        if(Render_DrawWireframeModel((struct RenderAnimFrame *)mesh.bytes,&point,0x400000,2048,ordering,arena.bytes,sizeof(arena.bytes)))valid=0;
        mesh.bytes[52]=0;
        for(unsigned i=0;i<3;++i){s16 zero=0;memcpy(mesh.bytes+28+i*8+2,&zero,2);}
        point.z=0;
        for(unsigned z=20000;z<=40000;z+=20000){
            view.t[2]=z;
            for(unsigned i=0;i<4096;++i)ordering[i]=(RenderNativeTag){0xFFFFFF,0xAB};
            if(Render_DrawWireframeModel((struct RenderAnimFrame *)mesh.bytes,&point,0x400000,0,ordering,arena.bytes,sizeof(arena.bytes)))valid=0;
            for(unsigned i=0;i<4096;++i)if(ordering[i].address!=0xFFFFFF || ordering[i].length!=0xAB)valid=0;
        }
        memset(mesh.bytes,0,sizeof(mesh.bytes));mesh.bytes[1]=255;
        memset(g_WireScratch.screenXY,0xA5,sizeof(saved_xy));memset(g_WireScratch.depth,0xA5,sizeof(saved_depth));
        if(Render_DrawWireframeModel((struct RenderAnimFrame *)mesh.bytes,&point,0x400000,0,ordering,arena.bytes,0) ||
           g_WireScratch.screenXY[255]!=0xA5A5A5A5 || (u16)g_WireScratch.depth[255]!=0xA5A5)valid=0;
    }
wire_restore:
#undef WIRE_PUT
    g_ViewMatrixSlot.value=saved_view;
    memcpy(g_WireScratch.screenXY,saved_xy,sizeof(saved_xy));memcpy(g_WireScratch.depth,saved_depth,sizeof(saved_depth));
    if(!valid || hash!=UINT64_C(0xdd1b134b66169daf)){
        fprintf(stderr,"Original wireframe renderer: samples=%u hash=%016llx valid=%d\n",samples,(unsigned long long)hash,valid);return 0;}
    puts("Original wireframe renderer: 17280 USA CPU/GTE-bridge cases, native LINE_G2/draw-mode payloads and full OT chains match");return 1;
}

static int original_wireframe_animation(void)
{
    Pe1GameState saved_state=g_GameState;GeomScrollRecord saved_scroll=g_GeomScroll;
    Combatant *saved_actor=g_ActiveActor;FieldActor *saved_player=g_PlayerEntity;
    int saved_offset=g_DrawPacketOffset,saved_slot=g_ActiveDrawSlot,saved_frame=g_BattleAttackAnimFrame;
    Combatant actor={0};BattleAction action={0};FieldActor player={0};actor.action=&action;
    GteMatrix view={0};view.m[0][0]=view.m[1][1]=view.m[2][2]=4096;
    g_ViewMatrixSlot.value=&view;g_ActiveActor=&actor;g_PlayerEntity=&player;
    RenderNativeTag ordering[4096];
    union {uintptr_t alignment;u8 bytes[0x4000];} arena;
    const int distances[]={-32768,-1,0,1,200,32767};
    const int frames[]={1,2,9,10,11,127,INT_MAX};
    const int modes[]={0,16,32,48};
    u32 saved_xy[256];s16 saved_depth[256];
    memcpy(saved_xy,g_WireScratch.screenXY,sizeof(saved_xy));memcpy(saved_depth,g_WireScratch.depth,sizeof(saved_depth));
    uint64_t hash=UINT64_C(14695981039346656037);int valid=1;unsigned samples=0;
#define WIRE_PUT(value,bytes) do {uint64_t v_=(uint64_t)(value); \
    for(unsigned b_=0;b_<(bytes);++b_){hash=(hash^(u8)v_)*UINT64_C(1099511628211);v_>>=8;}}while(0)
    for(unsigned slot=0;slot<2;++slot)for(unsigned mode=0;mode<4;++mode)
    for(unsigned distance=0;distance<6;++distance)for(unsigned step=0;step<7;++step)for(unsigned camera=0;camera<2;++camera){
        for(unsigned i=0;i<32;++i){Psyz_GteDataWrite(i,0);Psyz_GteCtrlWrite(i,0);}
        Psyz_GteCtrlWrite(26,320);
        union {u32 alignment;u8 bytes[128];} frame={0};
        frame.bytes[1]=3;frame.bytes[6]=16;frame.bytes[10]=1;frame.bytes[19]=2;
        const s16 xyz[][3]={{-100,-100,0},{100,0,0},{0,0,100}};
        for(unsigned i=0;i<3;++i){memcpy(frame.bytes+28+i*8,xyz[i],sizeof(xyz[i]));frame.bytes[28+i*8+6]=i%2;}
        memcpy(frame.bytes+52,(u8[]){0,1,2,0},4);memcpy(frame.bytes+56,(u8[]){255,17,129,18},4);
        frame.bytes[67]=frame.bytes[71]=33;
        player.pos_x=(s32)(((u32)(camera?50:0)<<16)|0x1234);
        player.pos_y=(s32)(((u32)(camera?-300:-25)<<16)|0x1234);
        player.pos_z=(s32)(((u32)(camera?8000:1000)<<16)|0x1234);
        actor.stateFlags=modes[mode];action.range=distances[distance];
        g_GameState.room_geometry_table=frame.bytes;g_DrawPacketOffset=0;g_ActiveDrawSlot=slot;g_BattleAttackAnimFrame=frames[step];
        for(unsigned i=0;i<2;++i){g_OtBufferTable[i]=(u8 *)ordering;g_RenderAnimBufferBase[i]=arena.bytes;}
        g_GeomScroll.state.position.screenOffsetX=camera?140:160;g_GeomScroll.state.position.screenOffsetY=camera?100:112;
        memset(arena.bytes,0xA5,sizeof(arena.bytes));
        memset(g_WireScratch.screenXY,0xA5,sizeof(saved_xy));memset(g_WireScratch.depth,0xA5,sizeof(saved_depth));
        for(unsigned i=0;i<4096;++i)ordering[i]=(RenderNativeTag){0xFFFFFF,0xAB};
        Render_AnimationFrame();int used=g_DrawPacketOffset;
        u8 *cur=arena.bytes;unsigned count=0,old_used=0;RenderNativeTag *packets[16];
        if(used<0 || used>(int)sizeof(arena.bytes)){valid=0;goto wire_restore;}
        while(cur<arena.bytes+used){
            RenderNativeTag *tag=(RenderNativeTag *)cur;
            if(count>=16 || (tag->length!=0 && tag->length!=1 && tag->length!=4)){valid=0;goto wire_restore;}
            packets[count++]=tag;old_used+=(tag->length+1)*4;
            cur+=tag->length==0?sizeof(RenderNativeTag):tag->length==1?sizeof(RenderDrawModePacket):sizeof(FieldLineG2Packet);
        }
        if(cur!=arena.bytes+used){valid=0;goto wire_restore;}
        WIRE_PUT(old_used,4);WIRE_PUT(g_BattleAttackAnimFrame,4);
        WIRE_PUT(Psyz_GteCtrlRead(24),4);WIRE_PUT(Psyz_GteCtrlRead(25),4);
        for(unsigned i=0;i<3;++i){WIRE_PUT(g_WireScratch.screenXY[i],4);WIRE_PUT(g_WireScratch.depth[i],2);}
        for(unsigned i=56;i<60;++i)WIRE_PUT(frame.bytes[i],1);
        for(unsigned i=0;i<count;++i){RenderNativeTag *tag=packets[i];WIRE_PUT(tag->length,1);
            const u8 *payload=(const u8 *)tag+sizeof(*tag);for(unsigned j=0;j<tag->length*4;++j)WIRE_PUT(payload[j],1);}
        for(unsigned layer=0;layer<4096;++layer){
            uintptr_t link=ordering[layer].address;unsigned links=0;
            if(ordering[layer].length!=0xAB)valid=0;
            if(link==0xFFFFFF)continue;WIRE_PUT(layer,2);
            while(link!=0xFFFFFF){
                unsigned id=0;while(id<count && (uintptr_t)packets[id]!=link)++id;
                if(id==count || ++links>=20){valid=0;goto wire_restore;}
                WIRE_PUT(id,1);link=packets[id]->address;
            }
            WIRE_PUT(links,1);
        }
        WIRE_PUT(65535,2);++samples;
    }
wire_restore:
#undef WIRE_PUT
    g_GameState=saved_state;g_GeomScroll=saved_scroll;g_ActiveActor=saved_actor;g_PlayerEntity=saved_player;
    g_DrawPacketOffset=saved_offset;g_ActiveDrawSlot=saved_slot;g_BattleAttackAnimFrame=saved_frame;
    memcpy(g_WireScratch.screenXY,saved_xy,sizeof(saved_xy));memcpy(g_WireScratch.depth,saved_depth,sizeof(saved_depth));
    if(!valid || hash!=UINT64_C(0xabf9dfc230518165)){
        fprintf(stderr,"Original wireframe animation: samples=%u hash=%016llx valid=%d\n",samples,(unsigned long long)hash,valid);return 0;}
    puts("Original wireframe animation: 672 USA CPU/GTE-bridge frames, canonical actor range/position, both buffers, OT depth and counter wrapping match");return 1;
}


static int original_textbox_renderer(PeDisc *disc,PeDiscFile *exe)
{
    u8 authored[sizeof(g_FieldFontTextureState)]={0};u8 spacing[sizeof(g_TextboxGlyphSpacing)]={0};
    if(!pe_disc_read(disc,exe,0x91644-0x10000+0x800,authored,sizeof(authored)) ||
       memcmp(authored,&g_FieldFontTextureState,sizeof(authored)) ||
       !pe_disc_read(disc,exe,0x916A0-0x10000+0x800,spacing,sizeof(spacing)) ||
       memcmp(spacing,g_TextboxGlyphSpacing,sizeof(spacing))){fprintf(stderr,"original textbox authored tables differ (font=%d spacing=%d)\n",memcmp(authored,&g_FieldFontTextureState,sizeof(authored)),memcmp(spacing,g_TextboxGlyphSpacing,sizeof(spacing)));return 0;}
    TextboxEntry saved_entries[4];memcpy(saved_entries,g_TextboxEntries,sizeof(saved_entries));
    TextboxGlyphPacket saved_choice[2];memcpy(saved_choice,g_TextboxChoiceCursor,sizeof(saved_choice));
    RenderModeTilePacket saved_fog[2];memcpy(saved_fog,g_TextboxFogTilePackets,sizeof(saved_fog));
    RenderBufferPrefix saved_buffers=D_800B0E38;
    u8 *saved_source=g_TextboxMessageSource,*saved_cursor=g_DrawPacketCursor,*saved_base=g_DrawPacketArenaBase;
    u32 *saved_order=g_DrawOrderingTableEntry;
    int saved_slot=g_ActiveDrawSlot,saved_collision=g_ScriptCollisionFlag,saved_dim=g_DrawTextDimmed;
    u32 saved_pad=g_FieldPadPressed,saved_color=g_DrawPrimColor,saved_shade=g_DrawColorShaded;
    u32 saved_blend[]={g_DrawBlendColorPair[0],g_DrawBlendColorPair[1]};
    u8 saved_globals[]={g_DialogueChoiceCursor,(u8)g_MenuEquipSlotIndex,g_TextboxKernGlyph0F,g_PlayerMovementEnabled,
        g_TextboxGlyphColorR,g_TextboxGlyphColorG,g_TextboxGlyphColorB,
        g_TextboxHighlightColorR,g_TextboxHighlightColorG,g_TextboxHighlightColorB};
    int saved_x=g_TextCursorX,saved_y=g_TextCursorY,saved_stack[28];
    memcpy(saved_stack,g_TextCursorStackBottom,sizeof(saved_stack));u16 *saved_vertex=g_DrawVertexWritePtr;
    struct {u8 before[16];TextboxGlyphPacket packets[TEXTBOX_GLYPH_PACKET_CAPACITY];u8 after[16];} glyph_storage;
    TextboxGlyphPacket *glyphs=glyph_storage.packets;
    union {uintptr_t align;u8 bytes[0x4000];} arena;
    RenderNativeTag ordering[8];u8 source[1024];
    const u8 special[][32]={
        {250,247,250,255},{251,4,1,251,0,251,1,251,2,251,3,251,5,2,255},
        {251,8,76,251,8,255},{251,9,3,1,247,2,247,3,255},{1,248,2,255},{1,249},
        {1,251,6,2,255},{1,251,7,3,2,255}};
    const u8 extended[]={0,1,20,21,127,255},fb[]={10,19,20,127,255};
    uint64_t hash=UINT64_C(14695981039346656037);int valid=1;unsigned frames=0;
#define TEXT_PUT(value,bytes) do {uint64_t v_=(uint64_t)(value); \
    for(unsigned b_=0;b_<(bytes);++b_){hash=(hash^(u8)v_)*UINT64_C(1099511628211);v_>>=8;}}while(0)
#define TEXT_PAYLOAD(packet,kind) do { \
    RenderNativeTag *t_=(RenderNativeTag *)(packet);TEXT_PUT(kind==1?6:kind==2?5:t_->length,1); \
    const u8 *p_=(const u8 *)(packet)+sizeof(RenderNativeTag); \
    if(kind){ \
        unsigned expected_=(sizeof(RenderTexturePagePacket)+(kind==1?16:12))/4; \
        if(t_->length!=expected_)valid=0; \
        for(unsigned j_=0;j_<4;++j_)TEXT_PUT(p_[j_],1); \
        const u8 *end_=(const u8 *)(packet)+sizeof(RenderTexturePagePacket)+sizeof(RenderNativeTag); \
        for(const u8 *zero_=p_+4;zero_<end_;++zero_){ \
            u8 expected_byte_=0; \
            if(sizeof(uintptr_t)==8 && zero_>=end_-4) \
                expected_byte_=(u8)(UINT32_C(0x005a4d50)>>(8*(zero_-(end_-4)))); \
            if(*zero_!=expected_byte_)valid=0;} \
        p_=end_; \
        for(unsigned j_=0;j_<(kind==1?16:12);++j_)TEXT_PUT(p_[j_],1); \
    }else for(unsigned j_=0;j_<t_->length*4;++j_)TEXT_PUT(p_[j_],1); \
}while(0)
    for(unsigned msg=0;msg<104;++msg)for(unsigned slot=0;slot<2;++slot)
    for(unsigned style=0;style<4;++style)for(unsigned state=1;state<=2;++state)
    for(unsigned variant=0;variant<4;++variant){
        memcpy(&g_FieldFontTextureState,authored,sizeof(authored));
        if(variant&1)for(unsigned i=0;i<4;++i){
            FieldBgTextureSlot *t=&g_FieldBgTextureSlots[i];
            t->tpage=BG_SLOT_TPAGE(t->x,t->y);t->clut=BG_SLOT_CLUT(t->clut_x,t->clut_y);
            if(g_TextboxFontPages[i].tpage!=t->tpage || g_TextboxFontPages[i].clut!=t->clut)valid=0;
        }
        memset(g_TextboxEntries,0xA5,sizeof(g_TextboxEntries));
        memset(g_TextboxChoiceCursor,0xA5,sizeof(g_TextboxChoiceCursor));
        memset(g_TextboxFogTilePackets,0xA5,sizeof(g_TextboxFogTilePackets));
        Render_SetupFogLayer(source);
        for(unsigned i=0;i<4;++i){TextboxEntry *e=&g_TextboxEntries[i];
            TEXT_PUT(e->state,1);TEXT_PUT(e->style,1);TEXT_PUT(e->background,1);
            TEXT_PUT(e->control.flags,4);TEXT_PUT(e->page_id,2);}
        for(unsigned i=0;i<2;++i){TEXT_PAYLOAD(&g_TextboxChoiceCursor[i],1);TEXT_PAYLOAD(&g_TextboxFogTilePackets[i],2);}
        memset(source,0,sizeof(source));source[0]=255;source[1]=254;source[2]=7;
        if(msg<78){source[3]=msg;source[4]=255;}
        else if(msg<86)memcpy(source+3,special[msg-78],sizeof(*special));
        else if(msg<98){source[3]=msg<92?252:253;source[4]=extended[(msg-86)%6];source[5]=255;}
        else if(msg<103){source[3]=251;source[4]=fb[msg-98];source[5]=255;}
        else memcpy(source+3,(u8[]){1,247,2,255},4);
        memset(arena.bytes,0xA5,sizeof(arena.bytes));memset(&glyph_storage,0xA5,sizeof(glyph_storage));
        for(unsigned i=0;i<8;++i)ordering[i]=(RenderNativeTag){0xFFFFFF,0xAB};
        for(unsigned i=0;i<2;++i){D_800B0E38.ordering[i]=ordering;D_800B0E38.other_buffers[1+i]=(char *)glyphs;}
        g_DrawPacketCursor=g_DrawPacketArenaBase=arena.bytes;g_DrawOrderingTableEntry=(u32 *)ordering;
        g_DrawPrimColor=0x112233;g_DrawColorShaded=0x445566;g_DrawTextDimmed=0;
        g_DrawBlendColorPair[0]=0x102030;g_DrawBlendColorPair[1]=0x405060;
        g_ActiveDrawSlot=slot;g_ScriptCollisionFlag=variant==3;
        g_FieldPadPressed=(u32[]){0,0x100,0x20,8}[variant];
        g_DialogueChoiceCursor=variant%3;g_MenuEquipSlotIndex=7;g_TextboxKernGlyph0F=variant%2;
        g_TextboxHighlightColorR=17;g_TextboxHighlightColorG=34;g_TextboxHighlightColorB=51;
        TextboxEntry *e;
        for(unsigned entry=0;entry<4;++entry){
            if(msg!=103 && entry!=variant%4)continue;
        e=&g_TextboxEntries[entry];
        e->state=state;e->message=source+3;e->style=style;e->background=1;
        e->control.flags=((variant&1)<<25)|((variant>>1)<<16)|((variant%4)<<8);e->page_id=7;
        e->x=43;e->y=57;e->width=260;e->height=32;
        memcpy(e->numbers,(u8[]){1,2,3,4,5,3,9,8,7,6,5,2},12);
        }
        Menu_DrawTextboxEntries();
        for(unsigned i=0;i<4;++i){e=&g_TextboxEntries[i];TEXT_PUT(e->state,1);
            TEXT_PUT(e->message?(u32)(e->message-source):UINT32_MAX,4);TEXT_PUT(e->control.flags,4);}
        TEXT_PUT(g_DialogueChoiceCursor,1);TEXT_PUT(g_MenuEquipSlotIndex,1);
        TEXT_PUT(g_TextboxGlyphColorR,1);TEXT_PUT(g_TextboxGlyphColorG,1);TEXT_PUT(g_TextboxGlyphColorB,1);
        for(unsigned layer=0;layer<8;++layer){
            uintptr_t link=ordering[layer].address;unsigned links=0;
            while(link!=0xFFFFFF){
                unsigned kind=0,id=0;void *packet=(void *)link;
                if(link>=(uintptr_t)glyphs && link<(uintptr_t)(glyphs+TEXTBOX_GLYPH_PACKET_CAPACITY)){
                    kind=1;id=(link-(uintptr_t)glyphs)/sizeof(*glyphs);
                }else if(link>=(uintptr_t)g_TextboxChoiceCursor && link<(uintptr_t)(g_TextboxChoiceCursor+2)){
                    kind=1;id=1000+(link-(uintptr_t)g_TextboxChoiceCursor)/sizeof(*g_TextboxChoiceCursor);
                }else if(link>=(uintptr_t)g_TextboxFogTilePackets && link<(uintptr_t)(g_TextboxFogTilePackets+2)){
                    kind=2;id=1010+(link-(uintptr_t)g_TextboxFogTilePackets)/sizeof(*g_TextboxFogTilePackets);
                }else{
                    u8 *cur=arena.bytes;unsigned old_offset=0;
                    while(cur<g_DrawPacketCursor && (uintptr_t)cur!=link){
                        RenderNativeTag *t=(RenderNativeTag *)cur;
                        if(t->length!=1 && t->length!=4){valid=0;break;}
                        old_offset+=t->length==1?8:20;
                        cur+=t->length==1?sizeof(RenderDrawModePacket):sizeof(RenderColorTrianglePacket);
                    }
                    if((uintptr_t)cur!=link || cur>=g_DrawPacketCursor){valid=0;break;}
                    id=2000+old_offset/4;
                }
                if(++links>=400){valid=0;break;}
                TEXT_PUT(id,2);TEXT_PAYLOAD(packet,kind);link=((RenderNativeTag *)packet)->address;
            }
            TEXT_PUT(links,2);if(ordering[layer].length!=0xAB)valid=0;
        }
        for(unsigned i=0;i<16;++i)if(glyph_storage.before[i]!=0xA5 || glyph_storage.after[i]!=0xA5)valid=0;
        ++frames;if(!valid)goto text_restore;
    }
    /* Preserve the original number of glyph slots after pointer widening;
     * overlong strings must stop without touching the adjacent work arena. */
    for(unsigned kind=0;kind<6;++kind){
        memset(&glyph_storage,0xA5,sizeof(glyph_storage));memset(g_TextboxEntries,0,sizeof(g_TextboxEntries));
        for(unsigned i=0;i<8;++i)ordering[i]=(RenderNativeTag){0xFFFFFF,0xAB};
        g_ActiveDrawSlot=0;g_ScriptCollisionFlag=0;g_FieldPadPressed=0;
        TextboxEntry *e=&g_TextboxEntries[0];e->state=2;e->style=2;e->message=source;
        unsigned bytes=0;
        if(kind==0)for(unsigned i=0;i<340;++i)source[bytes++]=1;
        else if(kind==1)for(unsigned i=0;i<120;++i)source[bytes++]=250;
        else if(kind==2)for(unsigned i=0;i<340;++i){source[bytes++]=252;source[bytes++]=1;}
        else if(kind==3)source[bytes++]=78;
        else{
            for(unsigned i=0;i<6;++i){source[bytes++]=251;source[bytes++]=8;}
            for(unsigned i=0;i<5;++i){e->numbers[i].digits[0]=1;e->numbers[i].count=kind==4?6:1;}
        }
        source[bytes]=255;Menu_DrawTextboxEntries();
        uintptr_t link=ordering[1].address;unsigned count=0,expected=kind<3?TEXTBOX_GLYPH_PACKET_CAPACITY:kind==5?5:0;
        while(link!=0xFFFFFF){
            if(count>=expected || link!=(uintptr_t)&glyphs[expected-count-1]){valid=0;break;}
            link=((RenderNativeTag *)link)->address;++count;
        }
        if(count!=expected)valid=0;
        for(unsigned i=0;i<16;++i)if(glyph_storage.before[i]!=0xA5 || glyph_storage.after[i]!=0xA5)valid=0;
    }
text_restore:
    memcpy(&g_FieldFontTextureState,authored,sizeof(authored));
#undef TEXT_PAYLOAD
#undef TEXT_PUT
    memcpy(g_TextboxEntries,saved_entries,sizeof(saved_entries));memcpy(g_TextboxChoiceCursor,saved_choice,sizeof(saved_choice));
    memcpy(g_TextboxFogTilePackets,saved_fog,sizeof(saved_fog));D_800B0E38=saved_buffers;
    g_TextboxMessageSource=saved_source;g_DrawPacketCursor=saved_cursor;g_DrawPacketArenaBase=saved_base;g_DrawOrderingTableEntry=saved_order;
    g_ActiveDrawSlot=saved_slot;g_ScriptCollisionFlag=saved_collision;g_DrawTextDimmed=saved_dim;
    g_FieldPadPressed=saved_pad;g_DrawPrimColor=saved_color;g_DrawColorShaded=saved_shade;
    memcpy(g_DrawBlendColorPair,saved_blend,sizeof(saved_blend));g_DialogueChoiceCursor=saved_globals[0];g_MenuEquipSlotIndex=saved_globals[1];
    g_TextboxKernGlyph0F=saved_globals[2];g_PlayerMovementEnabled=saved_globals[3];
    g_TextboxGlyphColorR=saved_globals[4];g_TextboxGlyphColorG=saved_globals[5];g_TextboxGlyphColorB=saved_globals[6];
    g_TextboxHighlightColorR=saved_globals[7];g_TextboxHighlightColorG=saved_globals[8];g_TextboxHighlightColorB=saved_globals[9];
    g_TextCursorX=saved_x;g_TextCursorY=saved_y;memcpy(g_TextCursorStackBottom,saved_stack,sizeof(saved_stack));g_DrawVertexWritePtr=saved_vertex;
    if(!valid || hash!=UINT64_C(0xa279a1e503166b41)){
        fprintf(stderr,"original textbox renderer mismatch: frames=%u hash=%016llx valid=%d\n",frames,(unsigned long long)hash,valid);return 0;}
    puts("Original textbox renderer: 6656 USA MIPS frames, all 78 base glyphs, extended pages, names, numbers, colors, choices, sections, delays, fog and both buffers match");
    return 1;
}

static int original_scan_overlay(PeDisc *disc, PeDiscFile *exe)
{
    u8 custom[]={1,255};
    struct {u8 *data;u32 address;size_t size;} texts[]={
        {g_AnalyzeHpUnknownText,0x91464,16},{g_AnalyzeHpFormatText,0x91474,12},
        {(u8 *)g_TextSusceptibleToPoison,0x91480,44},
        {(u8 *)g_TextSusceptibleToAcid,0x914AC,40},
        {(u8 *)g_AnalyzeTranquilizerWeakTexts,0x914D4,40},
        {(u8 *)g_AnalyzeHeatWeakTexts,0x914FC,36},
        {(u8 *)g_AnalyzeColdWeakTexts,0x91520,36},
        {(u8 *)g_AnalyzeStealWeakTexts,0x91544,44},
        {(u8 *)g_AnalyzeConfusionWeakTexts,0x91570,42}};
    u8 *messages[]={g_AnalyzeHpUnknownText,g_AnalyzeHpFormatText,
        g_TextSusceptibleToPoison[0],g_TextSusceptibleToPoison[1],
        g_TextSusceptibleToAcid[0],g_TextSusceptibleToAcid[1],
        g_AnalyzeTranquilizerWeakTexts[0],g_AnalyzeTranquilizerWeakTexts[1],
        g_AnalyzeHeatWeakTexts[0],g_AnalyzeHeatWeakTexts[1],
        g_AnalyzeColdWeakTexts[0],g_AnalyzeColdWeakTexts[1],
        g_AnalyzeStealWeakTexts[0],g_AnalyzeStealWeakTexts[1],
        g_AnalyzeConfusionWeakTexts[0],g_AnalyzeConfusionWeakTexts[1],custom};
    for(unsigned i=0;i<sizeof(texts)/sizeof(*texts);++i){
        u8 raw[44];
        if(!pe_disc_read(disc,exe,texts[i].address-0x10000+0x800,raw,texts[i].size) ||
           memcmp(raw,texts[i].data,texts[i].size))return 0;
    }
    TextboxEntry saved_entries[4];memcpy(saved_entries,g_TextboxEntries,sizeof(saved_entries));
    TextboxRect saved_rect=g_TextboxPendingRect;
    SavePromptBlock saved_scan=g_BattleScanPanel;
    u8 saved_active=g_BattleSaveOverlayActive,saved_timer=g_SaveOverlayTimer;
    u8 saved_choice=g_DialogueChoiceCursor,saved_mode=g_MenuActiveMode;
    s8 saved_equip=g_MenuEquipSlotIndex;
    u8 *saved_message=g_CurItemEffectData,*saved_cursor=g_DrawPacketCursor,*saved_base=g_DrawPacketArenaBase;
    unsigned *saved_order=g_DrawOrderingTableEntry;
    int saved_window=g_SaveMetadataWindowIndex,saved_dim=g_DrawTextDimmed;
    int saved_x=g_TextCursorX,saved_y=g_TextCursorY;
    u32 saved_color=g_DrawPrimColor,saved_shade=g_DrawColorShaded;
    u32 saved_blend[]={g_DrawBlendColorPair[0],g_DrawBlendColorPair[1]};
    int saved_stack[28];memcpy(saved_stack,g_TextCursorStackBottom,sizeof(saved_stack));
    u16 *saved_vertex=g_DrawVertexWritePtr;
    union {uintptr_t align;u8 bytes[0x4000];} arena;
    RenderNativeTag ordering;
    EnemyCombatant core={0};BattleEntity target={0};
    const u32 weaknesses[]={0,0x1000,0x2000,0x4000,0x8000,0x10000,0x20000,0x40000,0x7F000};
    const unsigned timers[]={0,1,75,255};
    uint64_t hash=UINT64_C(14695981039346656037);int valid=1;
#define SCAN_PUT(value,bytes) do {uint64_t v_=(uint64_t)(value); \
    for(unsigned b_=0;b_<(bytes);++b_){hash=(hash^(u8)v_)*UINT64_C(1099511628211);v_>>=8;}}while(0)
    for(unsigned sample=0;sample<18560;++sample){
        unsigned n=sample<18432?sample:sample-18432;
        unsigned window=n%2;n/=2;unsigned mode=n%4;n/=4;
        memset(arena.bytes,0xA5,sizeof(arena.bytes));ordering=(RenderNativeTag){0xFFFFFF,0xAB};
        g_DrawPacketCursor=g_DrawPacketArenaBase=arena.bytes;g_DrawOrderingTableEntry=(unsigned *)&ordering;
        g_DrawPrimColor=0x112233;g_DrawColorShaded=0x445566;g_DrawTextDimmed=0;
        g_DrawBlendColorPair[0]=0x102030;g_DrawBlendColorPair[1]=0x405060;
        g_SaveMetadataWindowIndex=window;g_MenuActiveMode=mode;
        memset(&g_TextboxPendingRect,0xA5,sizeof(g_TextboxPendingRect));
        g_DialogueChoiceCursor=3;g_MenuEquipSlotIndex=7;
        memset(g_TextboxEntries,0xA5,sizeof(g_TextboxEntries));
        for(unsigned i=0;i<4;++i){g_TextboxEntries[i].message=custom;g_TextboxEntries[i].control.flags=-1;}
        if(sample<18432){
            unsigned which=n%4;n/=4;unsigned timer=timers[n%4];n/=4;
            u32 weak=weaknesses[n%9];n/=9;unsigned prompt=n%4;unsigned phase=n/4;
            g_BattleSaveOverlayActive=0;g_SaveOverlayTimer=7;
            g_BattleScanPanel.prompt.word=0x48000000|weak|(prompt<<10)|(phase<<8)|timer;
            core.hpAlive=which==2?-1:123;core.hpAliveCap=456;
            target.core=which==1?NULL:&core;g_BattleScanPanel.target=which?&target:NULL;
            Save_DrawSlotMetadata();
        }else{
            g_SaveOverlayTimer=timers[n%4];g_BattleSaveOverlayActive=n/4;
            g_BattleScanPanel.prompt.word=0x12345678;g_CurItemEffectData=custom;
            Menu_SaveOverlayDraw();
        }
        SCAN_PUT(g_BattleScanPanel.prompt.word,4);SCAN_PUT(g_BattleSaveOverlayActive,1);
        SCAN_PUT(g_SaveOverlayTimer,1);SCAN_PUT(g_DialogueChoiceCursor,1);SCAN_PUT(g_MenuEquipSlotIndex,1);
        for(unsigned i=0;i<4;++i){
            TextboxEntry *e=&g_TextboxEntries[i];unsigned id=0;
            for(unsigned j=0;j<17;++j)if(e->message==messages[j])id=j+1;
            SCAN_PUT(e->state,1);SCAN_PUT(id,1);SCAN_PUT(e->style,1);SCAN_PUT(e->background,1);
            SCAN_PUT(e->control.flags,4);SCAN_PUT(e->page_id,2);SCAN_PUT(e->x,2);SCAN_PUT(e->y,2);
            SCAN_PUT(e->width,2);SCAN_PUT(e->height,2);
            const u8 *numbers=(const u8 *)e->numbers;for(unsigned j=0;j<12;++j)SCAN_PUT(numbers[j],1);
        }
        SCAN_PUT(g_TextboxPendingRect.x,2);SCAN_PUT(g_TextboxPendingRect.y,2);
        SCAN_PUT(g_TextboxPendingRect.width,2);SCAN_PUT(g_TextboxPendingRect.height,2);
        SCAN_PUT(g_TextCursorX,4);SCAN_PUT(g_TextCursorY,4);
        RenderNativeTag *packets[16];unsigned count=0;u8 *cur=arena.bytes;
        while(cur<g_DrawPacketCursor){
            RenderNativeTag *tag=(RenderNativeTag *)cur;
            if(count>=16 || (tag->length!=1 && tag->length!=4)){valid=0;break;}
            packets[count++]=tag;SCAN_PUT(tag->length,1);
            const u8 *payload=cur+sizeof(*tag);
            for(unsigned j=0;j<tag->length*4;++j)SCAN_PUT(payload[j],1);
            cur+=tag->length==1?sizeof(RenderDrawModePacket):sizeof(RenderColorTrianglePacket);
        }
        if(cur!=g_DrawPacketCursor || ordering.length!=0xAB)valid=0;
        for(u8 *guard=g_DrawPacketCursor;guard<arena.bytes+sizeof(arena.bytes);++guard)if(*guard!=0xA5)valid=0;
        SCAN_PUT(count,1);uintptr_t link=ordering.address;unsigned links=0;
        while(link!=0xFFFFFF){
            unsigned i=0;while(i<count && link!=(uintptr_t)packets[i])++i;
            if(i==count || links>=count){valid=0;break;}
            SCAN_PUT(i,1);link=packets[i]->address;++links;
        }
        SCAN_PUT(links,1);
        if(!valid)break;
    }
#undef SCAN_PUT
    /* Allocation pressure at every native alignment near the arena end. */
    for(unsigned remaining=0;remaining<=512;remaining+=sizeof(uintptr_t))
    for(unsigned kind=0;kind<4;++kind){
        memset(arena.bytes,0xA5,sizeof(arena.bytes));ordering=(RenderNativeTag){0xFFFFFF,0xAB};
        g_DrawPacketCursor=arena.bytes+sizeof(arena.bytes)-remaining;
        g_DrawPacketArenaBase=arena.bytes;g_DrawOrderingTableEntry=(unsigned *)&ordering;
        u8 *start=g_DrawPacketCursor;
        if(kind<2)Draw_AllocColorGradient(320,20,NULL,kind);
        else if(kind==2)Draw_AllocColorTri(320,20,0);
        else Draw_EmitWipeBarRect(320,20,0);
        if(g_DrawPacketCursor<start || g_DrawPacketCursor>arena.bytes+sizeof(arena.bytes) || ordering.length!=0xAB)valid=0;
        for(u8 *guard=arena.bytes;guard<start;++guard)if(*guard!=0xA5)valid=0;
        uintptr_t link=ordering.address;unsigned links=0;
        while(link!=0xFFFFFF){
            if(link<(uintptr_t)start || link+sizeof(RenderNativeTag)>(uintptr_t)g_DrawPacketCursor || ++links>20){valid=0;break;}
            RenderNativeTag *tag=(RenderNativeTag *)link;
            if(tag->length<1 || tag->length>4 || link+sizeof(*tag)+tag->length*4>(uintptr_t)g_DrawPacketCursor){valid=0;break;}
            link=tag->address;
        }
    }
    memcpy(g_TextboxEntries,saved_entries,sizeof(saved_entries));g_TextboxPendingRect=saved_rect;
    g_BattleScanPanel=saved_scan;g_BattleSaveOverlayActive=saved_active;g_SaveOverlayTimer=saved_timer;
    g_DialogueChoiceCursor=saved_choice;g_MenuEquipSlotIndex=saved_equip;g_MenuActiveMode=saved_mode;
    g_CurItemEffectData=saved_message;g_DrawPacketCursor=saved_cursor;g_DrawPacketArenaBase=saved_base;
    g_DrawOrderingTableEntry=saved_order;g_SaveMetadataWindowIndex=saved_window;g_DrawTextDimmed=saved_dim;
    g_TextCursorX=saved_x;g_TextCursorY=saved_y;g_DrawPrimColor=saved_color;g_DrawColorShaded=saved_shade;
    memcpy(g_DrawBlendColorPair,saved_blend,sizeof(saved_blend));
    memcpy(g_TextCursorStackBottom,saved_stack,sizeof(saved_stack));g_DrawVertexWritePtr=saved_vertex;
    if(!valid || hash!=UINT64_C(0x2c8a283ef8f19f95)){
        fprintf(stderr,"original Scan/message overlay mismatch: %016llx valid=%d\n",(unsigned long long)hash,valid);return 0;
    }
    puts("original Scan/message overlays: 18560 USA MIPS cases, native wipe payloads and OT chains");
    return 1;
}

static int original_hud_packet_updates(void)
{
    extern void Battle_DrawSpellName(void);
    extern void Battle_DrawStatusSymbol(s8);
    struct {void *data;size_t bytes,stride;unsigned id,kind;} pools[]={
        {g_BattleStatusValueDigits,sizeof(g_BattleStatusValueDigits),sizeof(RenderPagedSpritePacket),1,0},
        {g_BattleRemainingActionSprites,sizeof(g_BattleRemainingActionSprites),sizeof(RenderPagedSpritePacket),17,0},
        {g_BattleStatusValueMarkers,sizeof(g_BattleStatusValueMarkers),sizeof(RenderPagedSpritePacket),18,0},
        {g_BattleStatusSymbolSprites,sizeof(g_BattleStatusSymbolSprites),sizeof(RenderPagedSpritePacket),19,0},
        {g_BattlePanelDigits,sizeof(g_BattlePanelDigits),sizeof(RenderPagedSpritePacket),20,0},
        {g_BattleStatusBarTiles,sizeof(g_BattleStatusBarTiles),sizeof(RenderColorTilePacket),0,1},
        {g_StatusMarkerPackets,sizeof(g_StatusMarkerPackets),sizeof(RenderPagedSpritePacket),25,0},
    };
    Combatant actor={0};BattleAction action={0};actor.action=&action;
    Combatant *old_actor=g_ActiveActor;int old_slot=g_ActiveDrawSlot,old_panel=g_BattleStatusPanelSlot;
    u8 old_remaining=g_BattleRemainingActions,old_countdown=g_BattleSpritePrimCountdown;
    RenderNativeTag *old_ordering[]={D_800B0E38.ordering[0],D_800B0E38.ordering[1]};
    RenderNativeTag ordering[2][8];
    for(unsigned i=0;i<7;++i)memset(pools[i].data,0xA5,pools[i].bytes);
    memset(g_BattleStatusPointerSprites,0xA5,sizeof(g_BattleStatusPointerSprites));Battle_DrawHPBar();
    for(unsigned slot=0;slot<2;++slot)D_800B0E38.ordering[slot]=ordering[slot];
    g_ActiveActor=&actor;uint64_t hash=UINT64_C(14695981039346656037);int valid=1;
    const s16 anchors[][2]={{0,0},{221,177},{-32768,32767}};
    const u8 reds[]={0,128,255};
#define HUD_SETUP(slot,anchor) do { \
    for(unsigned layer=0;layer<8;++layer)ordering[slot][layer]=(RenderNativeTag){0xFFFFFF,0xAB}; \
    g_ActiveDrawSlot=slot;g_BattleStatusPanelSlot=0; \
    g_BattleStatusBarTiles[slot*3].x0=anchors[anchor][0];g_BattleStatusBarTiles[slot*3].y0=anchors[anchor][1]; \
    g_BattleStatusPointerSprites[slot].sprite.color.bytes.r=reds[anchor]; \
    g_BattleStatusPointerSprites[slot].sprite.color.bytes.g=17;g_BattleStatusPointerSprites[slot].sprite.color.bytes.b=231; \
} while(0)
#define HUD_BYTE(value) (hash=(hash^(u8)(value))*UINT64_C(1099511628211))
#define HUD_COLLECT(slot) do { \
    for(unsigned layer=0;layer<8;++layer) { \
        struct {unsigned pool,index;} nodes[16];unsigned count=0;uintptr_t address=ordering[slot][layer].address; \
        while(address!=0xFFFFFF) { \
            unsigned pool=0;for(;pool<7;++pool) { \
                uintptr_t base=(uintptr_t)pools[pool].data; \
                if(address>=base && address<base+pools[pool].bytes && !((address-base)%pools[pool].stride))break; \
            } \
            if(pool==7 || count>=16) {valid=0;break;} \
            nodes[count].pool=pool;nodes[count].index=(address-(uintptr_t)pools[pool].data)/pools[pool].stride;++count; \
            memcpy(&address,(const void *)address,sizeof(address)); \
        } \
        if(ordering[slot][layer].length!=0xAB)valid=0;HUD_BYTE(count); \
        for(unsigned i=0;i<count;++i) { \
            unsigned pool=nodes[i].pool;u8 *packet=(u8 *)pools[pool].data+nodes[i].index*pools[pool].stride; \
            HUD_BYTE(pools[pool].id);HUD_BYTE(nodes[i].index); \
            if(pools[pool].kind) { \
                for(unsigned byte=0;byte<12;++byte)HUD_BYTE(packet[sizeof(RenderNativeTag)+byte]); \
            } else { \
                for(unsigned byte=0;byte<4;++byte)HUD_BYTE(packet[sizeof(RenderNativeTag)+byte]); \
                for(unsigned byte=0;byte<16;++byte)HUD_BYTE(packet[sizeof(RenderTexturePagePacket)+sizeof(RenderNativeTag)+byte]); \
            } \
        } \
    } \
    for(unsigned byte=0;byte<4;++byte)HUD_BYTE((u32)g_BattleStatusPanelSlot>>(byte*8)); \
} while(0)
    for(unsigned slot=0;slot<2;++slot)for(unsigned anchor=0;anchor<3;++anchor)
    for(unsigned category=0;category<4;++category)for(unsigned mode=0;mode<4;++mode)for(unsigned left=0;left<11;++left) {
        HUD_SETUP(slot,anchor);action.attackWord=category<<20;action.turnWord=mode<<6;g_BattleRemainingActions=left;
        Battle_DrawSpellName();HUD_COLLECT(slot);
    }
    for(unsigned slot=0;slot<2;++slot)for(unsigned anchor=0;anchor<3;++anchor)for(unsigned which=0;which<9;++which) {
        HUD_SETUP(slot,anchor);Battle_DrawStatusSymbol(which);HUD_COLLECT(slot);
    }
    const s16 offsets[]={0,30,-32768,32767};
    for(unsigned slot=0;slot<2;++slot)for(unsigned anchor=0;anchor<3;++anchor)
    for(unsigned value=0;value<100;++value)for(unsigned offset=0;offset<4;++offset) {
        HUD_SETUP(slot,anchor);Battle_DrawStatusValue(value,offsets[offset]);HUD_COLLECT(slot);
    }
    const s16 values[]={-32768,-1,0,1,9,10,99,100,999,1000,9999,32767};
    const u8 timers[]={0,1,15,29,30,255};const u16 positions[][2]={{0,0},{123,234},{65535,32768}};
    for(unsigned slot=0;slot<2;++slot)for(unsigned panel=0;panel<10;++panel)for(unsigned mode=0;mode<2;++mode)
    for(unsigned style=0;style<4;++style)for(unsigned value=0;value<12;++value)
    for(unsigned timer=0;timer<6;++timer)for(unsigned position=0;position<3;++position) {
        HUD_SETUP(slot,position);g_BattleStatusPanelSlot=panel;
        BattleStatusPanel input={values[value],positions[position][0],positions[position][1],timers[timer],style};
        Battle_DrawStatusPanel(mode,&input);HUD_COLLECT(slot);
    }
    const int bottoms[]={0,24,28,32,40,127,128,255};
    for(unsigned slot=0;slot<2;++slot)for(unsigned anchor=0;anchor<3;++anchor)for(unsigned bottom=0;bottom<8;++bottom) {
        HUD_SETUP(slot,anchor);Battle_LayoutStatusPrimRow(bottoms[bottom]);HUD_COLLECT(slot);
    }
    const u8 countdowns[]={0,1,30,255};
    for(unsigned slot=0;slot<2;++slot)for(unsigned position=0;position<3;++position)for(unsigned timer=0;timer<4;++timer) {
        HUD_SETUP(slot,position);g_StatusMarkerPackets[slot].sprite.x=positions[position][0];
        g_StatusMarkerPackets[slot].sprite.y=positions[position][1];g_BattleSpritePrimCountdown=countdowns[timer];
        Draw_QueuePrimitive();HUD_COLLECT(slot);HUD_BYTE(g_BattleSpritePrimCountdown);
    }
#undef HUD_COLLECT
#undef HUD_BYTE
#undef HUD_SETUP
    g_ActiveActor=old_actor;g_ActiveDrawSlot=old_slot;g_BattleStatusPanelSlot=old_panel;g_BattleRemainingActions=old_remaining;g_BattleSpritePrimCountdown=old_countdown;
    D_800B0E38.ordering[0]=old_ordering[0];D_800B0E38.ordering[1]=old_ordering[1];
    if(!valid || hash!=UINT64_C(0xfa7672b3016c55b0)) {
        fprintf(stderr,"Original HUD update hash %016llx differs from USA MIPS\n",(unsigned long long)hash);return 0;
    }
    puts("Original HUD updates: 1,056 action lists, 54 symbols, 2,400 values and 34,560 floating/MISS panels, 48 layouts and 24 markers match unhooked USA MIPS");
    return 1;
}

static int original_hud_packet_initialization(void)
{
    struct {void *data;size_t bytes,stride;unsigned kind,count;} pools[]={
        {g_BattleStatusBarTiles,sizeof(g_BattleStatusBarTiles),sizeof(RenderColorTilePacket),3,6},
        {g_BattleStatusValueDigits,sizeof(g_BattleStatusValueDigits),sizeof(RenderPagedSpritePacket),0,4},
        {g_BattleTargetHighlightSprites,sizeof(g_BattleTargetHighlightSprites),sizeof(RenderPagedSpritePacket),0,6},
        {g_BattleStatusPointerSprites,sizeof(g_BattleStatusPointerSprites),sizeof(RenderPagedSpritePacket),0,2},
        {g_BattleStatusValueLabelSprites,sizeof(g_BattleStatusValueLabelSprites),sizeof(RenderPagedSpritePacket),0,2},
        {g_BattleAmmoLabelSprites,sizeof(g_BattleAmmoLabelSprites),sizeof(RenderPagedSpritePacket),0,2},
        {g_BattleAmmoDigitSprites,sizeof(g_BattleAmmoDigitSprites),sizeof(RenderPagedSpritePacket),0,8},
        {g_BattleStatusBackgrounds,sizeof(g_BattleStatusBackgrounds),sizeof(BattleModeTilePacket),1,2},
        {g_BattleAtbTracks,sizeof(g_BattleAtbTracks),sizeof(RenderColorTilePacket),3,2},
        {g_BattleAtbLabels,sizeof(g_BattleAtbLabels),sizeof(RenderPagedSpritePacket),0,2},
        {g_BattlePeLabels,sizeof(g_BattlePeLabels),sizeof(RenderPagedSpritePacket),0,2},
        {g_BattleAtbGauges,sizeof(g_BattleAtbGauges),sizeof(RenderGouraudQuad),2,2},
        {g_BattlePeGauges,sizeof(g_BattlePeGauges),sizeof(RenderGouraudQuad),2,4},
        {g_BattleAtbMarkers,sizeof(g_BattleAtbMarkers),sizeof(RenderPagedSpritePacket),0,2},
        {g_BattleHpDigits,sizeof(g_BattleHpDigits),sizeof(RenderPagedSpritePacket),0,8},
        {g_BattleMaxHpDigits,sizeof(g_BattleMaxHpDigits),sizeof(RenderPagedSpritePacket),0,10},
        {g_BattleHpLabels,sizeof(g_BattleHpLabels),sizeof(RenderPagedSpritePacket),0,2},
        {g_BattleRemainingActionSprites,sizeof(g_BattleRemainingActionSprites),sizeof(RenderPagedSpritePacket),0,20},
        {g_BattleStatusValueMarkers,sizeof(g_BattleStatusValueMarkers),sizeof(RenderPagedSpritePacket),0,2},
        {g_BattleStatusSymbolSprites,sizeof(g_BattleStatusSymbolSprites),sizeof(RenderPagedSpritePacket),0,2},
        {g_BattlePanelDigits,sizeof(g_BattlePanelDigits),sizeof(RenderPagedSpritePacket),0,100},
        {g_BattleEndPanels,sizeof(g_BattleEndPanels),sizeof(RenderTexturedQuad),4,2},
        {g_BattleStatusPointerLines,sizeof(g_BattleStatusPointerLines),sizeof(RenderLinePacket),5,4},
        {g_BattleStatusPointerTriangles,sizeof(g_BattleStatusPointerTriangles),sizeof(BattleStatusPointerPrim),6,2},
        {g_StatusIconPackets,sizeof(g_StatusIconPackets),sizeof(RenderPagedSpritePacket),0,26},
        {g_StatusMarkerPackets,sizeof(g_StatusMarkerPackets),sizeof(RenderPagedSpritePacket),0,2},
    };
    for(unsigned p=0;p<sizeof(pools)/sizeof(*pools);++p)memset(pools[p].data,0xA5,pools[p].bytes);
    Battle_DrawHPBar();
    uint64_t hash=UINT64_C(14695981039346656037);
    for(unsigned p=0;p<sizeof(pools)/sizeof(*pools);++p)for(unsigned i=0;i<pools[p].count;++i) {
        u8 *packet=(u8 *)pools[p].data+i*pools[p].stride;
        if(pools[p].kind<2) {
            RenderTexturePagePacket *page=(void *)packet;
            u8 *primitive=packet+sizeof(RenderTexturePagePacket);
            for(unsigned byte=0;byte<4;++byte)hash=(hash^((u8 *)&page->command)[byte])*UINT64_C(1099511628211);
            unsigned size=pools[p].kind==0?16:12;
            for(unsigned byte=0;byte<size;++byte)hash=(hash^primitive[sizeof(RenderNativeTag)+byte])*UINT64_C(1099511628211);
        } else {
            unsigned size=pools[p].kind==2?32:pools[p].kind==4?36:pools[p].kind==6?16:12;
            for(unsigned byte=0;byte<size;++byte)hash=(hash^packet[sizeof(RenderNativeTag)+byte])*UINT64_C(1099511628211);
        }
    }
    if(hash!=UINT64_C(0xfbe92ec193830469)) {
        fprintf(stderr,"Original HUD init hash %016llx differs from USA MIPS\n",(unsigned long long)hash);return 0;
    }
    if(g_BattleAtbGauges[0].tag.length!=8 || g_BattleAtbTracks[1].tag.length!=3 ||
       g_BattleHpDigits[1][3].page.length!=(sizeof(RenderTexturePagePacket)+16)/4 || g_BattleStatusBackgrounds[1].mode.tag.length!=(sizeof(RenderDrawModePacket)+12)/4 ||
       g_BattleStatusPointerLines[1][1].tag.length!=3 || g_BattleStatusPointerTriangles[1].tag.length!=4)return 0;
    BattleHudOrigin old_origin=g_HudOrigin;unsigned old_mode=Menu_GetActiveMode();
    for(int mode=-512;mode<512;++mode) {
        BattleHudOrigin before=g_HudOrigin;unsigned before_mode=Menu_GetActiveMode();
        Battle_SetWindowBoundsByMode(mode);unsigned index=(u8)mode;
        if(index<4) {
            if(g_HudOrigin.x!=g_MenuWindowModeBounds.values[index*2] || g_HudOrigin.y!=g_MenuWindowModeBounds.values[index*2+1] || Menu_GetActiveMode()!=index)return 0;
        } else if(memcmp(&before,&g_HudOrigin,sizeof(before)) || Menu_GetActiveMode()!=before_mode)return 0;
    }
    Combatant actor={0};Combatant *old_actor=g_ActiveActor;int old_slot=g_ActiveDrawSlot;
    RenderNativeTag *old_ordering[2]={D_800B0E38.ordering[0],D_800B0E38.ordering[1]};
    RenderNativeTag ordering[2][8]={0};g_ActiveActor=&actor;
    for(unsigned slot=0;slot<2;++slot)D_800B0E38.ordering[slot]=ordering[slot];
    hash=UINT64_C(14695981039346656037);int valid=1;
    for(unsigned slot=0;slot<2;++slot)for(unsigned mode=0;mode<4;++mode)for(unsigned flags=0;flags<0x2000;++flags) {
        g_ActiveDrawSlot=slot;Battle_SetWindowBoundsByMode(mode);actor.stateFlags=flags;
        ordering[slot][7].address=0xFFFFFF;ordering[slot][7].length=0xAB;
        Draw_StatusIcons();
        unsigned icons[13],count=0;uintptr_t address=ordering[slot][7].address;
        uintptr_t start=(uintptr_t)&g_StatusIconPackets[slot][0];
        while(address!=0xFFFFFF) {
            if(count>=13 || address<start || address>=start+sizeof(g_StatusIconPackets[slot]) ||
               (address-start)%sizeof(RenderPagedSpritePacket)) {valid=0;break;}
            unsigned index=(address-start)/sizeof(RenderPagedSpritePacket);icons[count++]=index;
            address=g_StatusIconPackets[slot][index].page.address;
        }
        if(ordering[slot][7].length!=0xAB)valid=0;
        hash=(hash^count)*UINT64_C(1099511628211);
        for(unsigned i=0;i<count;++i) {
            hash=(hash^icons[i])*UINT64_C(1099511628211);
            const u8 *payload=(const u8 *)&g_StatusIconPackets[slot][icons[i]].sprite+sizeof(RenderNativeTag);
            for(unsigned byte=0;byte<16;++byte)hash=(hash^payload[byte])*UINT64_C(1099511628211);
        }
    }
    uint64_t icon_hash=hash;hash=UINT64_C(14695981039346656037);
    BattleEntity entity={0};entity.core=&actor;BattleEntity *old_entity=D_8009D254;g_PlayerEntity=(FieldActor *)(&entity);
    u32 old_flags=g_GameStateFlags;int old_context=g_MenuItemContextFlag,old_frame=g_BattleFrameCounter;
    const u16 hp_cases[][2]={{0,0},{1,0},{9,9},{10,9},{99,10},{100,99},{999,100},{1000,999},{9999,0},{9999,9999}};
    const u32 states[]={0,0x400,0x800,0xC00,0x1FFF};const int frames[]={0,16,17,31};
    const int energy[]={-1,50<<16,101<<16};const u16 charge[]={0,4500,10000};
    for(unsigned slot=0;slot<2;++slot)for(unsigned battle=0;battle<2;++battle)for(unsigned context=0;context<2;++context)
    for(unsigned mode=0;mode<4;++mode)for(unsigned gauge=0;gauge<3;++gauge)for(unsigned hp=0;hp<10;++hp)
    for(unsigned state=0;state<5;++state)for(unsigned frame=0;frame<4;++frame) {
        for(unsigned layer=0;layer<8;++layer)ordering[slot][layer]=(RenderNativeTag){0xFFFFFF,0xAB};
        g_ActiveDrawSlot=slot;g_GameStateFlags=battle*2;g_MenuItemContextFlag=context*2;g_BattleFrameCounter=frames[frame];
        actor.exp_or_acc=energy[gauge];actor.hpAlive=charge[gauge];actor.maxAtk=100<<16;
        actor.maxHP=hp_cases[hp][0];actor.curHP=hp_cases[hp][1];actor.stateFlags=states[state];
        Battle_SetWindowBoundsByMode(mode);Battle_DrawActiveStatus();
        for(unsigned layer=0;layer<8;++layer) {
            struct {unsigned pool,index;} nodes[40];unsigned count=0;uintptr_t address=ordering[slot][layer].address;
            while(address!=0xFFFFFF) {
                unsigned pool=0;
                for(;pool<sizeof(pools)/sizeof(*pools);++pool) {
                    uintptr_t base=(uintptr_t)pools[pool].data;
                    if(address>=base && address<base+pools[pool].bytes && !( (address-base)%pools[pool].stride))break;
                }
                if(pool==sizeof(pools)/sizeof(*pools) || count>=40) {valid=0;break;}
                nodes[count].pool=pool;nodes[count].index=(address-(uintptr_t)pools[pool].data)/pools[pool].stride;++count;
                memcpy(&address,(const void *)address,sizeof(address));
            }
            if(ordering[slot][layer].length!=0xAB)valid=0;
            hash=(hash^count)*UINT64_C(1099511628211);
            for(unsigned i=0;i<count;++i) {
                unsigned pool=nodes[i].pool;u8 *packet=(u8 *)pools[pool].data+nodes[i].index*pools[pool].stride;
                hash=(hash^pool)*UINT64_C(1099511628211);hash=(hash^nodes[i].index)*UINT64_C(1099511628211);
                if(pools[pool].kind<2) {
                    for(unsigned byte=0;byte<4;++byte)hash=(hash^packet[sizeof(RenderNativeTag)+byte])*UINT64_C(1099511628211);
                    unsigned size=pools[pool].kind==0?16:12;
                    for(unsigned byte=0;byte<size;++byte)hash=(hash^packet[sizeof(RenderTexturePagePacket)+sizeof(RenderNativeTag)+byte])*UINT64_C(1099511628211);
                } else {
                    unsigned size=pools[pool].kind==2?32:pools[pool].kind==4?36:pools[pool].kind==6?16:12;
                    for(unsigned byte=0;byte<size;++byte)hash=(hash^packet[sizeof(RenderNativeTag)+byte])*UINT64_C(1099511628211);
                }
            }
        }
        for(unsigned byte=0;byte<10;++byte)hash=(hash^((u8 *)&actor.exp_or_acc)[byte])*UINT64_C(1099511628211);
    }
    g_PlayerEntity=(FieldActor *)(old_entity);g_GameStateFlags=old_flags;g_MenuItemContextFlag=old_context;g_BattleFrameCounter=old_frame;
    g_ActiveActor=old_actor;g_ActiveDrawSlot=old_slot;
    D_800B0E38.ordering[0]=old_ordering[0];D_800B0E38.ordering[1]=old_ordering[1];
    Battle_SetWindowBoundsByMode(old_mode);g_HudOrigin=old_origin;
    /* Complete icon order and sprite payloads from 65,536 actual executions
     * of USA Draw_StatusIcons 800334AC, with every 13-bit status combination. */
    if(!valid || icon_hash!=UINT64_C(0x34a3ba0320ab74ad) || hash!=UINT64_C(0xc21dbda617e554d5)) {
        fprintf(stderr,"Original HUD icon/active hashes %016llx/%016llx differ from USA MIPS\n",(unsigned long long)icon_hash,(unsigned long long)hash);return 0;
    }
    puts("Original HUD: 226 initialized packets, 65,536 icon lists and 19,200 active HUD frames match USA MIPS");
    return 1;
}

