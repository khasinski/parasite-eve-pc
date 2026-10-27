/* Exercise the menu actions after a real scene has already been rendered.
 * Startup-only comparisons miss precision lost in a deferred GPU queue. */
static int original_native_graphics_controls(const char *executable)
{
    const char *disc=getenv("PE_TEST_DISC");
    if(!disc || !*disc)return getenv("PE_REQUIRE_GAME_ASSETS")?1:77;
    if(!SDL_Init(SDL_INIT_EVENTS))return 1;
    char resolved[2][4096];const char *paths[]={executable,disc};
    char *cwd=SDL_GetCurrentDirectory();if(!cwd)return 1;
    for(unsigned i=0;i<2;++i){
        bool absolute=paths[i][0]=='/' || paths[i][0]=='\\' || (paths[i][0] && paths[i][1]==':');
        int n=snprintf(resolved[i],sizeof(resolved[i]),"%s%s",absolute?"":cwd,paths[i]);
        if(n<0 || (size_t)n>=sizeof(resolved[i])){SDL_free(cwd);return 1;}
    }
    SDL_free(cwd);
    char directory[4096],capture[4200];
    snprintf(directory,sizeof(directory),"%spe-graphics-%llx",SDL_GetBasePath(),(unsigned long long)SDL_GetPerformanceCounter());
    if(!SDL_CreateDirectory(directory))return 1;
    snprintf(capture,sizeof(capture),"%s/frame.ppm",directory);
    const char *args[]={resolved[0],"--disc",resolved[1],"--headless-gpu","--internal-resolution","4",
        "--geometry-correction","0","--antialiasing","0","--model-filtering","0",
        "--frame-limit","60000","--control-at","24000","--dump-frame",capture,
        "--pad-at","10000:8","--pad-at","10010:0","--pad-at","11000:16","--pad-at","11010:0",
        "--pad-at","11500:16384","--pad-at","11510:0",NULL};
    SDL_PropertiesID props=SDL_CreateProperties();
    SDL_SetPointerProperty(props,SDL_PROP_PROCESS_CREATE_ARGS_POINTER,(void *)args);
    SDL_SetStringProperty(props,SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING,directory);
    SDL_SetNumberProperty(props,SDL_PROP_PROCESS_CREATE_STDIN_NUMBER,SDL_PROCESS_STDIO_APP);
    SDL_SetNumberProperty(props,SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER,SDL_PROCESS_STDIO_APP);
    SDL_SetBooleanProperty(props,SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN,true);
    SDL_Process *child=SDL_CreateProcessWithProperties(props);SDL_DestroyProperties(props);if(!child)return 1;
    SDL_IOStream *input=SDL_GetProcessInput(child),*output=SDL_GetProcessOutput(child);
    const char *commands[]={"graphics 1 0 0\n","graphics 1 0 1\n","graphics 1 1 1\n",
        "graphics 0 0 0\n","graphics 1 1 1\n","advance 8 0\n","quit\n"};
    const char *expected[]={"\"geometry\":0,\"antialiasing\":0,\"filtering\":0",
        "\"geometry\":1,\"antialiasing\":0,\"filtering\":0",
        "\"geometry\":1,\"antialiasing\":0,\"filtering\":1",
        "\"geometry\":1,\"antialiasing\":1,\"filtering\":1",
        "\"geometry\":0,\"antialiasing\":0,\"filtering\":0",
        "\"geometry\":1,\"antialiasing\":1,\"filtering\":1",
        "\"geometry\":1,\"antialiasing\":1,\"filtering\":1"};
    unsigned char *images[7]={0};size_t sizes[7]={0};unsigned count=0;int ok=input && output;
    char *line=SDL_malloc(1024*1024);size_t used=0;char byte;
    if(!line)ok=0;
    while(ok){
        if(SDL_ReadIO(output,&byte,1)!=1){
            if(SDL_GetIOStatus(output)==SDL_IO_STATUS_NOT_READY){SDL_Delay(1);continue;}
            break;
        }
        if(used>=1024*1024-1){ok=0;break;}line[used++]=byte;
        if(byte!='\n')continue;
        line[used]=0;used=0;
        if(strstr(line,"runtime error:") || strstr(line,"ERROR: AddressSanitizer")){ok=0;break;}
        if(strncmp(line,"{\"event\":\"control\"",18))continue;
        if(count>=7 || !strstr(line,expected[count]) || !strstr(line,"\"rgb24\":0")){fprintf(stderr,"Unexpected graphics snapshot: %s",line);ok=0;break;}
        images[count]=SDL_LoadFile(capture,&sizes[count]);
        if(!images[count] || (count && sizes[count]!=sizes[0])){ok=0;break;}
        size_t length=strlen(commands[count]);
        if(SDL_WriteIO(input,commands[count],length)!=length){ok=0;break;}
        ++count;
    }
    if(!ok)SDL_KillProcess(child,true);
    int status=-1;SDL_WaitProcess(child,true,&status);SDL_DestroyProcess(child);SDL_free(line);
    ok=ok && status==0 && count==7;
    if(ok){
        for(unsigned option=1;option<=3;++option){
            size_t differences=0;
            for(size_t i=0;i<sizes[0];++i)differences+=images[option-1][i]!=images[option][i];
            printf("Live graphics option %u: %zu changed channels\n",option,differences);
            if(differences<1000)ok=0;
        }
        if(memcmp(images[0],images[4],sizes[0]) || memcmp(images[3],images[5],sizes[0]) ||
            memcmp(images[5],images[6],sizes[0]))ok=0;
    }
    for(unsigned i=0;i<7;++i)SDL_free(images[i]);
    if(!ok){fprintf(stderr,"Live graphics controls failed: status=%d captures=%u\n",status,count);return 1;}
    puts("Live graphics controls: all three options change real scene pixels; toggles are reversible and stable");
    return 0;
}

/* Use the actual New Game piano sequence and PCM mixer. Checking only the
 * video state missed music continuing after the film had already stopped. */
static int original_native_movie_skip(const char *executable)
{
    const char *disc=getenv("PE_TEST_DISC");
    if(!disc || !*disc)return getenv("PE_REQUIRE_GAME_ASSETS")?1:77;
    if(!SDL_Init(SDL_INIT_EVENTS))return 1;
    char resolved[2][4096];const char *paths[]={executable,disc};
    char *cwd=SDL_GetCurrentDirectory();if(!cwd)return 1;
    for(unsigned i=0;i<2;++i){
        bool absolute=paths[i][0]=='/' || paths[i][0]=='\\' || (paths[i][0] && paths[i][1]==':');
        int n=snprintf(resolved[i],sizeof(resolved[i]),"%s%s",absolute?"":cwd,paths[i]);
        if(n<0 || (size_t)n>=sizeof(resolved[i])){SDL_free(cwd);return 1;}
    }
    SDL_free(cwd);
    for(unsigned skip=0;skip<2;++skip){
        char directory[4096],capture[4200];
        snprintf(directory,sizeof(directory),"%spe-movie-skip-%llx-%u",SDL_GetBasePath(),
                 (unsigned long long)SDL_GetPerformanceCounter(),skip);
        if(!SDL_CreateDirectory(directory))return 1;
        snprintf(capture,sizeof(capture),"%s/audio.pcm",directory);
        const char *args[40]={resolved[0],"--disc",resolved[1],"--headless","--frame-limit","15008",
            "--dump-audio",capture,"--pad-at","10000:8","--pad-at","10010:0",
            "--pad-at","11000:16","--pad-at","11010:0","--pad-at","11500:16384","--pad-at","11510:0"};
        if(skip){args[20]="--pad-at";args[21]="14000:8";args[22]="--pad-at";args[23]="14008:0";}
        SDL_Environment *env=SDL_CreateEnvironment(true);if(!env)return 1;
        SDL_SetEnvironmentVariable(env,"PSYZ_AUDIO_CLOCK","video",true);
        SDL_SetEnvironmentVariable(env,"SDL_AUDIO_DRIVER","dummy",true);
        SDL_PropertiesID props=SDL_CreateProperties();
        SDL_SetPointerProperty(props,SDL_PROP_PROCESS_CREATE_ARGS_POINTER,(void *)args);
        SDL_SetPointerProperty(props,SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER,env);
        SDL_SetStringProperty(props,SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING,directory);
        SDL_SetNumberProperty(props,SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER,SDL_PROCESS_STDIO_APP);
        SDL_SetBooleanProperty(props,SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN,true);
        SDL_Process *child=SDL_CreateProcessWithProperties(props);
        SDL_DestroyProperties(props);SDL_DestroyEnvironment(env);if(!child)return 1;
        int status=-1;size_t bytes=0;char *output=SDL_ReadProcess(child,&bytes,&status);
        SDL_DestroyProcess(child);
        int ok=output && status==0 && strstr(output,"\"event\":\"frame_limit\"") &&
            strstr(output,skip?"\"video_state\":0":"\"video_state\":2") &&
            strstr(output,skip?"\"scene\":2818576456":"\"scene\":2819109064") &&
            !strstr(output,"runtime error:") && !strstr(output,"AddressSanitizer");
        /* The final second is beyond the release/reverb tail. The unskipped
         * control must be audible, so silence cannot pass with broken audio. */
        FILE *audio=fopen(capture,"rb");unsigned nonzero=0;
        unsigned char tail[44100*4];
        if(!audio)ok=0;
        else{
            if(fseek(audio,-(long)sizeof(tail),SEEK_END) || fread(tail,1,sizeof(tail),audio)!=sizeof(tail))ok=0;
            else for(size_t i=0;i<sizeof(tail);i+=2)nonzero+=(tail[i]!=0 || tail[i+1]!=0);
            if(fclose(audio))ok=0;
        }
        if(skip?nonzero!=0:nonzero<44100)ok=0;
        if(!ok)fprintf(stderr,"Movie skip %u failed: status=%d nonzero samples=%u (artifacts %s)\n%s\n",
                       skip,status,nonzero,directory,output?output:SDL_GetError());
        SDL_free(output);if(!ok)return 1;
        printf("Story movie %s: %u nonzero samples in final second\n",skip?"skipped":"playing",nonzero);
    }
    return 0;
}

static int original_native_control(const char *executable)
{
    const char *disc=getenv("PE_TEST_DISC");if(!disc || !*disc)return getenv("PE_REQUIRE_GAME_ASSETS")?1:77;
    if(!SDL_Init(SDL_INIT_EVENTS))return 1;
    char resolved[2][2048];const char *paths[]={executable,disc};char *cwd=SDL_GetCurrentDirectory();if(!cwd)return 1;
    for(unsigned i=0;i<2;++i){bool absolute=paths[i][0]=='/' || paths[i][0]=='\\' || (paths[i][0] && paths[i][1]==':');
        int n=snprintf(resolved[i],sizeof(resolved[i]),"%s%s",absolute?"":cwd,paths[i]);if(n<0 || n>=sizeof(resolved[i])){SDL_free(cwd);return 1;}}
    SDL_free(cwd);
    char final_command[127];memset(final_command,' ',sizeof(final_command)-1);
    memcpy(final_command,"advance 2 0",11);final_command[sizeof(final_command)-1]=0;
    const char *commands[]={"advance 2 0x4000\nadvance 4 0\n","quit\n","",
        "advance 2 0x4000","advance 2 0x4000\r\nquit",final_command,"advance 0 0\n","advance 7 0\n",
        "advance -1 0\n","advance 1 65536\n","advance 1 -1\n","advance 1 0 garbage\n","advance 2\n","unknown\n","graphics 2 0 0\n","graphics 0 0\n","graphics 0 0 0 trailing\n",NULL};
    char too_long[200];memset(too_long,'x',sizeof(too_long));too_long[sizeof(too_long)-1]=0;
    unsigned passed=0;
    for(unsigned trial=0;trial<sizeof(commands)/sizeof(*commands);++trial){
        char directory[2048],card[2080];int n=snprintf(directory,sizeof(directory),"%spe-control-%llx-%u",SDL_GetBasePath(),(unsigned long long)SDL_GetPerformanceCounter(),trial);
        if(n<0 || n>=sizeof(directory) || !SDL_CreateDirectory(directory))return 1;
        for(unsigned slot=0;slot<2;++slot){snprintf(card,sizeof(card),"%s/bu%u0",directory,slot);if(!SDL_CreateDirectory(card))return 1;}
        const char *args[]={resolved[0],"--disc",resolved[1],"--headless","--frame-limit","8","--control-at","2",NULL};
        SDL_PropertiesID props=SDL_CreateProperties();SDL_SetPointerProperty(props,SDL_PROP_PROCESS_CREATE_ARGS_POINTER,(void *)args);
        SDL_SetStringProperty(props,SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING,directory);
        SDL_SetNumberProperty(props,SDL_PROP_PROCESS_CREATE_STDIN_NUMBER,SDL_PROCESS_STDIO_APP);
        SDL_SetNumberProperty(props,SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER,SDL_PROCESS_STDIO_APP);
        SDL_SetBooleanProperty(props,SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN,true);
        SDL_Process *child=SDL_CreateProcessWithProperties(props);SDL_DestroyProperties(props);if(!child)return 1;
        SDL_IOStream *input=SDL_GetProcessInput(child);const char *command=commands[trial]?commands[trial]:too_long;size_t length=strlen(command);
        int wrote=input && SDL_WriteIO(input,command,length)==length;if(input)SDL_CloseIO(input);
        int status=-1;size_t bytes=0;char *output=SDL_ReadProcess(child,&bytes,&status);SDL_DestroyProcess(child);
        int expected=trial<6?0:2;int ok=wrote && output && status==expected && strstr(output,"\"event\":\"control\",\"vblanks\":2") && strstr(output,"\"control\":{");
        if(output && !strstr(output,"\"bonus_points\":"))ok=0;
        if(output){
            const char *hex=strstr(output,"\"special_records\":\"");
            if(!hex)ok=0;
            else{
                hex+=strlen("\"special_records\":\"");unsigned digits=0;
                while(hex[digits] && isxdigit((unsigned char)hex[digits]))++digits;
                if(digits!=9*32*2 || hex[digits]!='"')ok=0;
            }
            const char *keys[]={"\"category_counts\":[","\"storage\":[","\"special_storage\":[","\"stat_allocation\":["};
            const unsigned counts[]={6,100,82,7};
            for(unsigned field=0;field<4;++field){
                const char *cursor=strstr(output,keys[field]);
                if(!cursor){ok=0;continue;}cursor+=strlen(keys[field]);
                for(unsigned item=0;item<counts[field];++item){
                    char *end;unsigned long value=strtoul(cursor,&end,10);
                    if(end==cursor || value>65535 || *end!=(item+1==counts[field]?']':',')){ok=0;break;}
                    cursor=end+1;
                }
            }
            if(!strstr(output,"\"character\":{\"level_index\":") ||
               !strstr(output,"\"total_exp\":") || !strstr(output,"\"spell_flags\":"))ok=0;
        }
        if(output && (strstr(output,"runtime error:") || strstr(output,"AddressSanitizer") || strstr(output,"Unbound original")))ok=0;
        if(trial==0 && (!strstr(output,"\"event\":\"control\",\"vblanks\":4") || !strstr(output,"\"event\":\"frame_limit\",\"vblanks\":8")))ok=0;
        if(trial>=3 && trial<6 && !strstr(output,"\"event\":\"control\",\"vblanks\":4"))ok=0;
        if(trial>0 && output && strstr(output,"\"event\":\"frame_limit\""))ok=0;
        if(!ok){fprintf(stderr,"Native control case%u status%d: %.300s\n",trial,status,output?output:"");SDL_free(output);return 1;}
        SDL_free(output);++passed;
    }
    printf("Native headless control:%u stdin advance/quit/EOF and malformed/overflow command cases passed with private cards\n",passed);return 0;
}

static int native_diagnostic_hp_arguments(const char *executable)
{
    const char *bad[][9]={
        {"--diagnostic-hp-at",NULL},
        {"--diagnostic-hp-at","0:1:2",NULL},
        {"--diagnostic-hp-at","x",NULL},
        {"--diagnostic-hp-at","1:-1:2",NULL},
        {"--diagnostic-hp-at","1:65536:2",NULL},
        {"--diagnostic-hp-at","1:1:65536",NULL},
        {"--diagnostic-hp-at","1:1:2x",NULL},
        {"--diagnostic-hp-at","2147483648:1:2",NULL},
        {"--diagnostic-hp-at","1:1",NULL},
        {"--diagnostic-hp-at","2:1:2","--diagnostic-hp-at","2:1:2",NULL},
        {"--diagnostic-hp-at","2:1:2","--diagnostic-hp-at","1:1:2",NULL},
        {"--frame-limit","10","--diagnostic-hp-at","1:1:2",NULL},
        {"--headless","--diagnostic-hp-at","1:1:2",NULL},
        {"--headless","--frame-limit","10","--diagnostic-hp-at","11:1:2",NULL},
        {"--headless","--frame-limit","10","--diagnostic-hp-at","1:1:2",NULL}
    };
    SDL_Environment *environment=SDL_CreateEnvironment(true);
    if(!environment || !SDL_SetEnvironmentVariable(environment,"PSYZ_HEADLESS","0",true))return 0;
    int valid=1;
    for(unsigned row=0;row<sizeof(bad)/sizeof(bad[0]);++row){
        const char *args[16]={executable,"--disc","/nonexistent-diagnostic-image"};unsigned at=3;
        for(unsigned item=0;bad[row][item];++item)args[at++]=bad[row][item];
        SDL_PropertiesID props=SDL_CreateProperties();
        SDL_SetPointerProperty(props,SDL_PROP_PROCESS_CREATE_ARGS_POINTER,(void *)args);
        SDL_SetPointerProperty(props,SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER,environment);
        SDL_SetNumberProperty(props,SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER,SDL_PROCESS_STDIO_APP);
        SDL_SetBooleanProperty(props,SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN,true);
        SDL_Process *child=SDL_CreateProcessWithProperties(props);SDL_DestroyProperties(props);
        if(!child){valid=0;break;}
        int status=-1;size_t size=0;char *output=SDL_ReadProcess(child,&size,&status);SDL_DestroyProcess(child);
        if(status!=EXIT_FAILURE || !output || strstr(output,"ERROR: AddressSanitizer") || strstr(output,"runtime error:") ||
           (row==14?!strstr(output,"Cannot open image"):
            ((!strstr(output,"diagnostic") && !strstr(output,"Diagnostic")) || strstr(output,"Cannot open image")))){
            fprintf(stderr,"Diagnostic HP argument case %u status%d: %.200s\n",row,status,output?output:"");valid=0;
        }
        SDL_free(output);
    }
    SDL_DestroyEnvironment(environment);
    if(valid)puts("Native diagnostic HP arguments: 14 malformed/unordered/non-headless/out-of-cap schedules rejected; explicit headless schedule accepted before image access");
    return valid;
}

static int original_native_tutorial(const char *executable)
{
    const char *disc=getenv("PE_TEST_DISC"),*disc2=getenv("PE_TEST_DISC2");
    if(!disc || !*disc || !disc2 || !*disc2)return getenv("PE_REQUIRE_GAME_ASSETS")?1:77;
    if(!SDL_Init(SDL_INIT_EVENTS))return 1;
    char resolved[3][2048];const char *paths[]={executable,disc,disc2};
    char *cwd=SDL_GetCurrentDirectory();if(!cwd)return 1;
    for(unsigned i=0;i<3;++i){
        bool absolute=paths[i][0]=='/' || paths[i][0]=='\\' || (paths[i][0] && paths[i][1]==':');
        int n=snprintf(resolved[i],sizeof(resolved[i]),"%s%s",absolute?"":cwd,paths[i]);
        if(n<0 || (size_t)n>=sizeof(resolved[i])){SDL_free(cwd);return 1;}
    }
    SDL_free(cwd);
    char directory[2048],path[3][2080],card[2080];
    int n=snprintf(directory,sizeof(directory),"%spe-tutorial-%llx",SDL_GetBasePath(),(unsigned long long)SDL_GetPerformanceCounter());
    if(n<0 || (size_t)n>=sizeof(directory) || !SDL_CreateDirectory(directory))return 1;
    for(unsigned slot=0;slot<2;++slot){snprintf(card,sizeof(card),"%s/bu%u0",directory,slot);if(!SDL_CreateDirectory(card))return 1;}
    const char *names[]={"frame.vram","frame.ppm","audio.s16le"};
    for(unsigned i=0;i<3;++i)snprintf(path[i],sizeof(path[i]),"%s/%s",directory,names[i]);
    const char *args[]={resolved[0],"--disc",resolved[1],"--disc2",resolved[2],"--headless",
        "--control-at","20000","--frame-limit","200000","--dump-vram",path[0],"--dump-frame",path[1],"--dump-audio",path[2],
        "--pad-at","10000:8","--pad-at","10010:0","--pad-at","11000:0x40","--pad-at","11010:0",
        "--pad-at","11500:0x4000","--pad-at","11510:0",NULL};
    /* Original pad route: both controllers, icons, battle, save, return/title.
     * Captured from actual UI input, never from edited game or save state. */
    const char *commands=
        "advance 10 16384\nadvance 1200 0\nadvance 10 16384\nadvance 120 0\nadvance 10 16384\nadvance 120 0\n"
        "advance 10 16384\nadvance 120 0\nadvance 10 16384\nadvance 120 0\nadvance 10 16384\nadvance 120 0\n"
        "advance 10 16384\nadvance 120 0\nadvance 10 16384\nadvance 120 0\nadvance 10 16384\nadvance 120 0\n"
        "advance 10 16384\nadvance 120 0\nadvance 10 16384\nadvance 120 0\nadvance 10 16384\nadvance 120 0\n"
        "advance 10 16384\nadvance 120 0\nadvance 10 16384\nadvance 120 0\nadvance 10 16384\nadvance 120 0\n"
        "advance 10 16384\nadvance 120 0\nadvance 10 16384\nadvance 120 0\nadvance 10 16384\nadvance 120 0\n"
        "advance 10 8192\nadvance 600 0\nadvance 10 16384\nadvance 120 0\nadvance 10 16384\nadvance 120 0\n"
        "advance 10 16384\nadvance 120 0\nadvance 10 16384\nadvance 120 0\nadvance 10 16384\nadvance 120 0\n"
        "advance 10 16384\nadvance 120 0\nadvance 10 16384\nadvance 120 0\nadvance 10 16384\nadvance 120 0\n"
        "advance 10 16384\nadvance 120 0\nadvance 10 16384\nadvance 120 0\nadvance 10 16384\nadvance 120 0\n"
        "advance 10 16384\nadvance 120 0\nadvance 10 16384\nadvance 120 0\nadvance 10 16384\nadvance 120 0\n"
        "advance 300 0\nadvance 10 64\nadvance 120 0\nadvance 10 16384\nadvance 600 0\nadvance 10 16384\n"
        "advance 120 0\nadvance 10 16384\nadvance 120 0\nadvance 10 16384\nadvance 120 0\nadvance 10 16384\n"
        "advance 120 0\nadvance 600 0\nadvance 10 16384\nadvance 180 0\nadvance 10 16384\nadvance 180 0\n"
        "advance 10 16384\nadvance 180 0\nadvance 10 16384\nadvance 180 0\nadvance 300 0\nadvance 10 16384\n"
        "advance 600 0\nadvance 10 64\nadvance 120 0\nadvance 10 16384\nadvance 600 0\nadvance 10 16384\n"
        "advance 180 0\nadvance 10 16384\nadvance 180 0\nadvance 10 16384\nadvance 180 0\nadvance 10 16384\n"
        "advance 180 0\nadvance 300 0\nadvance 10 16384\nadvance 600 0\nadvance 10 16384\nadvance 600 0\n"
        "advance 10 64\nadvance 120 0\nadvance 10 16384\nadvance 600 0\nadvance 10 16384\nadvance 600 0\n"
        "advance 10 64\nadvance 120 0\nadvance 10 16384\nadvance 1800 0\nadvance 10 8\nadvance 900 0\nadvance 10 8\n"
        "advance 600 0\nquit\n";
    SDL_Environment *environment=SDL_CreateEnvironment(true);
    if(!environment || !SDL_SetEnvironmentVariable(environment,"PSYZ_AUDIO_CLOCK","video",true) ||
       !SDL_SetEnvironmentVariable(environment,"SDL_AUDIODRIVER","dummy",true)){SDL_DestroyEnvironment(environment);return 1;}
    SDL_PropertiesID props=SDL_CreateProperties();
    SDL_SetPointerProperty(props,SDL_PROP_PROCESS_CREATE_ARGS_POINTER,(void *)args);
    SDL_SetPointerProperty(props,SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER,environment);
    SDL_SetStringProperty(props,SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING,directory);
    SDL_SetNumberProperty(props,SDL_PROP_PROCESS_CREATE_STDIN_NUMBER,SDL_PROCESS_STDIO_APP);
    SDL_SetNumberProperty(props,SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER,SDL_PROCESS_STDIO_APP);
    SDL_SetBooleanProperty(props,SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN,true);
    SDL_Process *child=SDL_CreateProcessWithProperties(props);SDL_DestroyProperties(props);SDL_DestroyEnvironment(environment);
    if(!child)return 1;
    SDL_IOStream *input=SDL_GetProcessInput(child);size_t length=strlen(commands);
    int wrote=input && SDL_WriteIO(input,commands,length)==length;if(input)SDL_CloseIO(input);
    int status=-1;size_t bytes=0;char *output=SDL_ReadProcess(child,&bytes,&status);SDL_DestroyProcess(child);
    uint64_t state_hash=UINT64_C(14695981039346656037);unsigned checkpoints=0;
    for(const char *line=output;line && *line;){
        const char *end=strchr(line,'\n');if(!end)end=line+strlen(line);
        if(!strncmp(line,"{\"event\":\"control\",\"vblanks\":",29)){
            ++checkpoints;
            for(const char *p=line;p<end;++p)if(*p!='\r')state_hash=(state_hash^(unsigned char)*p)*UINT64_C(1099511628211);
            state_hash=(state_hash^'\n')*UINT64_C(1099511628211);
        }
        line=*end?end+1:NULL;
    }
    /* The graphics diagnostics object was added after the original capture.
     * Removing only that object reproduces the old 8d2f71e7d6a00792 hash;
     * game state, VRAM, rendered image and PCM remain unchanged. */
    int valid=wrote && output && status==0 && checkpoints==123 && state_hash==UINT64_C(0x522b9a0de95abb9c);
    char log_path[2080];snprintf(log_path,sizeof(log_path),"%s/game.log",directory);
    FILE *log=fopen(log_path,"wb");
    if(!log)valid=0;
    else{int error=output && fwrite(output,1,bytes,log)!=bytes;if(fclose(log))error=1;if(error)valid=0;}
    if(output && (strstr(output,"runtime error:") || strstr(output,"ERROR: AddressSanitizer") || strstr(output,"Unbound original")))valid=0;
    /* Port-generated regression capture, including audible FMV XA and the
     * independently checked SPU gain/envelope timing. State and image hashes
     * are unchanged; independent SPU/retail comparisons verify fidelity. */
    const uint64_t expected[]={UINT64_C(0xd5b236a1a9e02c16),UINT64_C(0x937c09a8e0d595c3),UINT64_C(0x7314664de6b6b4fc)};
    for(unsigned i=0;i<3;++i){
        FILE *file=fopen(path[i],"rb");if(!file){valid=0;continue;}
        uint64_t hash=UINT64_C(14695981039346656037);unsigned char buffer[65536];size_t count;
        while((count=fread(buffer,1,sizeof(buffer),file)))for(size_t j=0;j<count;++j)hash=(hash^buffer[j])*UINT64_C(1099511628211);
        int error=ferror(file);if(fclose(file))error=1;
        if(error || hash!=expected[i]){fprintf(stderr,"Tutorial %s hash %016llx\n",names[i],(unsigned long long)hash);valid=0;}
    }
    if(!valid)fprintf(stderr,"Tutorial status%d checkpoints%u state%016llx (artifacts %s)\n",status,checkpoints,(unsigned long long)state_hash,directory);
    SDL_free(output);
    if(valid)puts("Original Tutorial: four sections/both controllers, 123 full-state checkpoints, final VRAM/rendered image/complete PCM and return to title passed");
    return valid?0:1;
}

static int original_native_bootstrap(const char *executable,int field_control)
{
    const char *disc=getenv("PE_TEST_DISC");
    if(!disc || !*disc)return getenv("PE_REQUIRE_GAME_ASSETS")?1:77;
    if(!SDL_Init(SDL_INIT_EVENTS))return 1;
    /* A replay can use the original phone Save. Keep every child away from
     * the caller's memory cards, including when this fixture runs in the repo. */
    char resolved[2][2048];const char *paths[2]={executable,disc};
    char *cwd=SDL_GetCurrentDirectory();
    if(!cwd)return 1;
    for(unsigned i=0;i<2;++i){
        bool absolute=paths[i][0]=='/' || paths[i][0]=='\\' ||
            (paths[i][0] && paths[i][1]==':');
        int n=snprintf(resolved[i],sizeof(resolved[i]),"%s%s",absolute?"":cwd,paths[i]);
        if(n<0 || (size_t)n>=sizeof(resolved[i])){SDL_free(cwd);return 1;}
    }
    SDL_free(cwd);executable=resolved[0];disc=resolved[1];
    const char *base=SDL_GetBasePath();if(!base)return 1;
    unsigned first=field_control>=2?(unsigned)field_control+10:field_control?6:0;
    unsigned end=field_control>=2?first+1:field_control?12:6;
    for(unsigned input=first;input<end;++input){
        char directory[2048],capture[2080];
        int n=snprintf(directory,sizeof(directory),"%spe-native-%llx-%u",base,
            (unsigned long long)SDL_GetPerformanceCounter(),input);
        if(n<0 || (size_t)n>=sizeof(directory) || !SDL_CreateDirectory(directory))return 1;
        snprintf(capture,sizeof(capture),"%s/frame.vram",directory);
        const char *limit=input==23?"261381":input==22?"246050":input==21?"222996":input==20?"200826":input==19?"154203":input==18?"131932":input==17?"112000":input==16?"104000":input==15?"95751":input==14?"82000":input==13?"64554":input==12?"60634":input==11?"58000":input==10?"52000":input==9?"42000":input==8?"35500":input>=6?"33500":input==5?"30000":input==4?"24000":input>=2?"14000":input?"13000":"8000";
        const char *args[8192]={executable,"--disc",disc,"--headless","--frame-limit",limit,"--dump-vram",capture};
        const char *events[]={"10000:8","10010:0","11000:0x10","11010:0","11500:0x4000","11510:0"};
        unsigned at=8;
        for(unsigned event=0;event<(input==3?4u:input==2 || input>=4?6u:input?1u:0u);++event){
            unsigned index=input==3 && event>=2?event+2:event;
            args[at++]="--pad-at";args[at++]=events[index];
        }
        char scheduled[4096][32];
        if(input==1){
            for(unsigned event=0;event<128;++event){
                snprintf(scheduled[event],sizeof(scheduled[event]),"%u:8",10001+event);
                args[at++]="--pad-at";args[at++]=scheduled[event];
            }
        }
        if(input>=5){
#include "actor_animation/native_replay_schedules.inc"
        SDL_PropertiesID props=SDL_CreateProperties();
        SDL_SetPointerProperty(props,SDL_PROP_PROCESS_CREATE_ARGS_POINTER,(void *)args);
        SDL_SetStringProperty(props,SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING,directory);
        SDL_SetNumberProperty(props,SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER,SDL_PROCESS_STDIO_APP);
        SDL_SetBooleanProperty(props,SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN,true);
        SDL_Process *child=SDL_CreateProcessWithProperties(props);SDL_DestroyProperties(props);
        if(!child)return 1;
        int status=-1;size_t bytes=0;char *output=SDL_ReadProcess(child,&bytes,&status);
        SDL_DestroyProcess(child);
        int valid=output && status==0 && strstr(output,"\"event\":\"frame_limit\"") &&
            strstr(output,input==23?"\"vblanks\":261381":input==22?"\"vblanks\":246050":input==21?"\"vblanks\":222996":input==20?"\"vblanks\":200826":input==19?"\"vblanks\":154203":input==18?"\"vblanks\":131932":input==17?"\"vblanks\":112000":input==16?"\"vblanks\":104000":input==15?"\"vblanks\":95751":input==14?"\"vblanks\":82000":input==13?"\"vblanks\":64554":input==12?"\"vblanks\":60634":input==11?"\"vblanks\":58000":input==10?"\"vblanks\":52000":input==9?"\"vblanks\":42000":input==8?"\"vblanks\":35500":input>=6?"\"vblanks\":33500":input==5?"\"vblanks\":30000":input==4?"\"vblanks\":24000":input>=2?"\"vblanks\":14000":input?"\"vblanks\":13000":"\"vblanks\":8000");
        /* UBSan may report a recoverable failure while the child exits 0.
         * Keep stderr with the captured state and fail the replay on it. */
        if(output && (strstr(output,"runtime error:") || strstr(output,"AddressSanitizer:") ||
           strstr(output,"LeakSanitizer:") || strstr(output,"UndefinedBehaviorSanitizer:")))valid=0;
        if(!input) {
            /* The retail boot calls the three game-clock initializers, not
             * SDK SsInit. A stopped clock changes later scripted encounters. */
            const char *timers=output?strstr(output,"\"timers\":["):NULL;
            unsigned flags[3]={0};int current[3]={0},limit[3]={0};
            if(!timers || sscanf(timers,
                "\"timers\":[{\"flags\":%u,\"current\":%d,\"limit\":%d},"
                "{\"flags\":%u,\"current\":%d,\"limit\":%d},"
                "{\"flags\":%u,\"current\":%d,\"limit\":%d}",
                &flags[0],&current[0],&limit[0],&flags[1],&current[1],&limit[1],
                &flags[2],&current[2],&limit[2])!=9 || flags[0]!=1 || flags[1]!=1 || flags[2]!=1 ||
                limit[0]!=21600000 || limit[1] || limit[2]) {
                fprintf(stderr,"Original boot game clocks are not initialized\n");valid=0;
            }
        }
        if(input==1 && (!output || !strstr(output,"\"pad\":4") || !strstr(output,"\"video_state\":0") ||
           !strstr(output,"\"rgb24\":1")))valid=0;
        if(input==2){
            unsigned frames=0;int movie_frame=0;
            const char *game=output?strstr(output,"\"game_frame\":"):NULL;
            const char *movie=output?strstr(output,"\"movie_frame\":"):NULL;
            if(!output || !strstr(output,"\"scene\":2819109064") || !strstr(output,"\"movie\":2") ||
               !strstr(output,"\"video_state\":2") || !game || !movie ||
               sscanf(game,"\"game_frame\":%u",&frames)!=1 || frames<2 ||
               sscanf(movie,"\"movie_frame\":%d",&movie_frame)!=1 || movie_frame<2)valid=0;
        }
        if(input==3 && (!output || !strstr(output,"\"scene\":2839543880") ||
           !strstr(output,"\"video_state\":0") || !strstr(output,"\"rgb24\":0") ||
           !strstr(output,"\"game_frame\":0")))valid=0;
        if(input==4){
            unsigned frames=0;const char *game=output?strstr(output,"\"game_frame\":"):NULL;
            if(!output || !strstr(output,"\"scene\":2818576456") || !strstr(output,"\"video_state\":0") ||
               !strstr(output,"\"dialogue\":[{\"state\":2,\"page\":9}") ||
               !strstr(output,"\"type\":0,\"sub\":0,\"player\":true") || !game ||
               sscanf(game,"\"game_frame\":%u",&frames)!=1 || frames<4000)valid=0;
        }
        if(input==5 && (!output || !strstr(output,"\"scene\":2818576456") ||
           !strstr(output,"\"video_state\":0") || !strstr(output,"\"flags\":4") ||
           !strstr(output,"\"menu\":{\"mode\":2,\"selection\":17")))valid=0;
        if(input>=6 && input<9){
            int x=0,y=0,z=0,yaw=0;
            const char *player=output?strstr(output,"\"type\":0,\"sub\":0,\"player\":true"):NULL;
            const char *position=player?strstr(player,"\"position\":"):NULL;
            if(!output || !strstr(output,input==8?"\"scene\":2818572744":"\"scene\":2818572616") || !strstr(output,"\"video_state\":0") ||
               !strstr(output,"\"menu\":null") || !position ||
               sscanf(position,"\"position\":[%d,%d,%d],\"yaw\":%d",&x,&y,&z,&yaw)!=4 ||
               x!=(input==8?-17825792:-8323072+(input==7?20*FIELD_WALK_SPEED:0)) ||
               y!=(input==8?-262144:0) || z!=(input==8?178192384:-352387072) ||
               yaw!=(input==8?2048:input==7?3072:2144))valid=0;
        }
        if(input==9 && (!output || !strstr(output,"\"scene\":2818994504") ||
           !strstr(output,"\"movie\":3") || !strstr(output,"\"video_state\":2") ||
           !strstr(output,"\"movie_frame\":257") || !strstr(output,"\"rgb24\":1")))valid=0;
        if(input==10 && (!output || !strstr(output,"\"scene\":2818572872") ||
           !strstr(output,"\"video_state\":0") || !strstr(output,"\"dialogue\":[{\"state\":2,\"page\":33}") ||
           !strstr(output,"\"position\":[65142784,-23855104,457768960],\"yaw\":0")))valid=0;
        if(input==11 && (!output || !strstr(output,"\"scene\":2818572872") ||
           !strstr(output,"\"video_state\":0") || !strstr(output,"\"game_frame\":6211") ||
           !strstr(output,"\"dialogue\":[{\"state\":0,\"page\":35}") ||
           !strstr(output,"\"position\":[-134283264,-21360136,489422848],\"yaw\":0")))valid=0;
        if(input==12 && (!output || !strstr(output,"\"scene\":2818573000") ||
           !strstr(output,"\"video_state\":0") ||
           !strstr(output,"\"position\":[-107559689,-72810496,35984650]")))valid=0;
        if(input==13 && (!output || !strstr(output,"\"scene\":2818573000") ||
           !strstr(output,"\"flags\":2178,") || !strstr(output,"\"move_lock\":4") ||
           !strstr(output,"\"video_state\":0") || !strstr(output,"\"game_frame\":2138") ||
           !strstr(output,"\"dialogue\":[{\"state\":0,\"page\":44}") ||
           !strstr(output,"\"player\":true,\"flags\":0,\"position\":[1441792,-72810496,56885248]")))valid=0;
        if(input==14 && (!output || !strstr(output,"\"scene\":2818573000") ||
           !strstr(output,"\"flags\":0,\"draw_slot\"") || !strstr(output,"\"move_lock\":0") ||
           !strstr(output,"\"video_state\":0") || !strstr(output,"\"game_frame\":2305") ||
           !strstr(output,"\"dialogue\":[{\"state\":0,\"page\":60}") || strstr(output,"\"type\":2,\"sub\":0") ||
           !strstr(output,"\"player\":true,\"flags\":8,\"position\":[22609920,-72810496,41484288],\"yaw\":3625") ||
           !strstr(output,"\"combat\":{\"mode\":12,\"live_enemies\":0,\"player_hp\":14,\"player_max_hp\":45}")))valid=0;
        if(input==15 && (!output || !strstr(output,"\"scene\":2818573512") ||
           !strstr(output,"\"flags\":0,\"draw_slot\"") || !strstr(output,"\"move_lock\":0") ||
           !strstr(output,"\"game_frame\":504") || !strstr(output,"\"video_state\":0") ||
           !strstr(output,"\"dialogue\":[{\"state\":0,\"page\":-1}") ||
           !strstr(output,"\"player\":true,\"flags\":8,\"position\":[61903440,-21495808,-178751558],\"yaw\":3536") ||
           !strstr(output,"\"combat\":{\"mode\":12,\"live_enemies\":0,\"player_hp\":14,\"player_max_hp\":45}")))valid=0;
        if(input==16 && (!output || !strstr(output,"\"scene\":2818576712") ||
           !strstr(output,"\"flags\":0,\"draw_slot\"") || !strstr(output,"\"move_lock\":0") ||
           !strstr(output,"\"game_frame\":770") || !strstr(output,"\"video_state\":0") ||
           !strstr(output,"\"dialogue\":[{\"state\":0,\"page\":-1}") ||
           !strstr(output,"\"player\":true,\"flags\":8,\"position\":[19202048,0,170917888],\"yaw\":0") ||
           !strstr(output,"\"combat\":{\"mode\":12,\"live_enemies\":0,\"player_hp\":14,\"player_max_hp\":45}")))valid=0;
        if(input==17){
            unsigned hp=0,max_hp=0;
            const char *health=output?strstr(output,"\"player_hp\":"):NULL;
            if(!output || !strstr(output,"\"scene\":2818576712") ||
               !strstr(output,"\"video_state\":0") || !strstr(output,"\"move_lock\":0") ||
               !strstr(output,"\"combat\":{\"mode\":9,\"live_enemies\":0") ||
               !strstr(output,"\"menu\":null") || !health ||
               sscanf(health,"\"player_hp\":%u,\"player_max_hp\":%u",&hp,&max_hp)!=2 ||
               !hp || hp>max_hp)valid=0;
        }
        if(input==18 && (!output || !strstr(output,"\"scene\":2818580552") ||
           !strstr(output,"\"video_state\":0") || !strstr(output,"\"move_lock\":1") ||
           !strstr(output,"\"menu\":{\"mode\":1,\"selection\":55") ||
           !strstr(output,"\"player\":true,\"flags\":8,\"position\":[-33929350,0,19598750],\"yaw\":2048") ||
           !strstr(output,"\"combat\":{\"mode\":9,\"live_enemies\":0,\"player_hp\":45,\"player_max_hp\":45}") ||
           !strstr(output,"\"inventory\":{\"capacity\":10,\"slots\":[256,257,258,512,6,0,0")))valid=0;
        if(input==19 && (!output || !strstr(output,"\"scene\":2818577352") ||
           !strstr(output,"\"video_state\":0") || !strstr(output,"\"move_lock\":0") ||
           !strstr(output,"\"menu\":null") ||
           !strstr(output,"\"dialogue\":[{\"state\":0,\"page\":89}") ||
           !strstr(output,"\"player\":true,\"flags\":8,\"position\":[29818880,-262144,-48365568],\"yaw\":2048") ||
           !strstr(output,"\"combat\":{\"mode\":9,\"live_enemies\":0,\"player_hp\":29,\"player_max_hp\":49}") ||
           !strstr(output,"\"inventory\":{\"capacity\":11,\"slots\":[256,257,258,512,6,0,0")))valid=0;
        if(input==20){
            unsigned hp=0,max_hp=0;
            const char *health=output?strstr(output,"\"player_hp\":"):NULL;
            if(!output || !strstr(output,"\"scene\":2818580808") ||
               !strstr(output,"\"video_state\":0") || !strstr(output,"\"move_lock\":0") ||
               !strstr(output,"\"menu\":null") ||
               !strstr(output,"\"combat\":{\"mode\":9,\"live_enemies\":0") ||
               !strstr(output,"\"slots\":[256,257,258,512,6,200,201,0") || !health ||
               sscanf(health,"\"player_hp\":%u,\"player_max_hp\":%u",&hp,&max_hp)!=2 ||
               hp!=43 || max_hp!=53)valid=0;
            /* This exact phone-created payload also reloads in retail USA.
             * A visually correct route must not hide a failed disk write. */
            char save_path[2112];
            snprintf(save_path,sizeof(save_path),"%s/bu00/BASLUS-006620000000A",directory);
            FILE *save=fopen(save_path,"rb");
            uint64_t save_hash=UINT64_C(14695981039346656037);size_t save_bytes=0;
            if(save){
                int byte;
                while((byte=fgetc(save))!=EOF){
                    save_hash=(save_hash^(u8)byte)*UINT64_C(1099511628211);++save_bytes;
                }
                if(ferror(save))valid=0;
                fclose(save);
            }else valid=0;
            if(save_bytes!=8192 || save_hash!=UINT64_C(0xddbea58f2544300e))valid=0;
        }
        if(input==21 && (!output || !strstr(output,"\"scene\":2818970824") ||
           !strstr(output,"\"video_state\":0") || !strstr(output,"\"move_lock\":0") ||
           !strstr(output,"\"menu\":null") || !strstr(output,"\"dialogue\":[{\"state\":0,\"page\":133}") ||
           !strstr(output,"\"player\":true,\"flags\":8,\"position\":[0,0,137953280],\"yaw\":-451") ||
           !strstr(output,"\"player_hp\":18,\"player_max_hp\":53") ||
           !strstr(output,"\"inventory\":{\"capacity\":12,\"slots\":[256,257,258,512,6,200,201,0")))valid=0;
        if(input>=21){
            /* The original parrot death script sets global variable24 bit20.
             * Reaching Melissa alone must not satisfy this optional-fight gate. */
            const char *bank=output?strstr(output,"\"script_variables\":["):NULL;
            if(bank)bank=strchr(bank,'[')+1;
            for(unsigned i=0;bank && i<24;++i){bank=strchr(bank,',');if(bank)++bank;}
            unsigned flags=0;
            if(!bank || sscanf(bank,"%u",&flags)!=1 || !(flags&0x100000u))valid=0;
        }
        if(input==22 && (!output || !strstr(output,"\"scene\":2818584776") ||
           !strstr(output,"\"video_state\":0") || !strstr(output,"\"move_lock\":0") ||
           !strstr(output,"\"menu\":null") || !strstr(output,"\"dialogue\":[{\"state\":0,\"page\":11}") ||
           !strstr(output,"\"player\":true,\"flags\":1032,\"position\":[11141120,68681728,322442560],\"yaw\":2048") ||
           !strstr(output,"\"combat\":{\"mode\":9,\"live_enemies\":0,\"player_hp\":27,\"player_max_hp\":64}") ||
           !strstr(output,"\"inventory\":{\"capacity\":13,\"slots\":[256,257,258,512,0,200,201,0")))valid=0;
        if(input==23 && (!output || !strstr(output,"\"scene\":2818585160") ||
           !strstr(output,"\"video_state\":0") || !strstr(output,"\"move_lock\":5") ||
           !strstr(output,"\"menu\":null") || !strstr(output,"\"dialogue\":[{\"state\":2,\"page\":8}") ||
           !strstr(output,"\"player\":true,\"flags\":1280,\"position\":[-57212928,78643200,720896],\"yaw\":1024") ||
           !strstr(output,"\"combat\":{\"mode\":7,\"live_enemies\":1,\"player_hp\":61,\"player_max_hp\":67}") ||
           !strstr(output,"\"inventory\":{\"capacity\":14,\"slots\":[256,257,258,512,0,200,201,0")))valid=0;
        FILE *file=fopen(capture,"rb");uint64_t hash=UINT64_C(14695981039346656037);
        if(file){
            u8 block[4096];size_t total=0,n;
            while((n=fread(block,1,sizeof(block),file))!=0){total+=n;
                /* Original USA Disc 1, DuckStation software frame 16604,
                 * M010 dialogue 9: skin and cuffs of Aya's date. Allow two
                 * RGB555 levels for the current rasterizer's shading error;
                 * replacing the skin with neutral grey must fail. */
                static const u8 reference_hands[][5]={
                    {72,139,28,28,28},{108,141,30,30,30},
                    {72,141,27,21,17},{74,141,25,20,16},
                    {104,143,28,22,18},{106,143,26,20,17},
                    {108,143,25,19,16},{104,147,25,19,16}};
                if(input==4)for(unsigned p=0;p<sizeof(reference_hands)/sizeof(reference_hands[0]);++p){
                    size_t offset=((size_t)(224+reference_hands[p][1])*1024+reference_hands[p][0])*2;
                    if(offset>=total-n && offset+2<=total){
                        size_t at=offset-(total-n);u16 pixel=block[at]|((u16)block[at+1]<<8);
                        for(unsigned channel=0;channel<3;++channel){
                            int delta=((pixel>>(channel*5))&31)-reference_hands[p][channel+2];
                            if(delta < -2 || delta > 2){valid=0;fprintf(stderr,"M010 PS1 hand colour mismatch at %u,%u channel %u\n",reference_hands[p][0],reference_hands[p][1],channel);}
                        }
                    }
                }
                for(size_t i=0;i<n;++i)hash=(hash^block[i])*UINT64_C(1099511628211);}
            if(total!=1024*512*2 || ferror(file))valid=0;
            fclose(file);remove(capture);
        }else valid=0;
        /* Raster hashes through input18 were renewed after the PS1 attribute
         * precision fix. All original state assertions passed in both builds;
         * M115 additionally matches independent PS1 VRAM byte for byte.
         * Inputs19/20 now use running original boot clocks; the latter was
         * recorded again through another victory and a genuine phone Save.
         * Input21 also records the parrot victory/reward and second Melissa.
         * Input22 records both sewer victories and the M031 sloping floor. */
        if(!input && hash!=UINT64_C(0x6c2aef67a1f42efb))valid=0;
        if(input==4 && hash!=UINT64_C(0x1ada3f3398816f93))valid=0;
        if(input==5 && hash!=UINT64_C(0x0ec1e290a3492b00))valid=0;
        if(input==6 && hash!=UINT64_C(0x6d41a093b9d9479f))valid=0;
        if(input==7 && hash!=UINT64_C(0xf5956c8862619663))valid=0;
        if(input==8 && hash!=UINT64_C(0x254e0ccbfc436ec4))valid=0;
        if(input==9 && hash!=UINT64_C(0x4f2f3366d72018ad))valid=0;
        if(input==10 && hash!=UINT64_C(0xf0e363af6b7d37a7))valid=0;
        if(input==11 && hash!=UINT64_C(0x38674366879df19f))valid=0;
        if(input==12 && hash!=UINT64_C(0x15ae9dc0520a12d8))valid=0;
        if(input==13 && hash!=UINT64_C(0x458f8a58931d4e5a))valid=0;
        if(input==14 && hash!=UINT64_C(0xfd7ae4cac94eda93))valid=0;
        if(input==15 && hash!=UINT64_C(0x1a8d9ad75c8755ae))valid=0;
        if(input==16 && hash!=UINT64_C(0x75f57637532bf9c1))valid=0;
        if(input==19 && hash!=UINT64_C(0xee31b8302c6660a8))valid=0;
        if(input==20 && hash!=UINT64_C(0x77627bc3feb8cb34))valid=0;
        if(input==21 && hash!=UINT64_C(0xec3df138587c4889))valid=0;
        if(input==22 && hash!=UINT64_C(0xd3d2869131c3ae7))valid=0;
        if(input==23 && hash!=UINT64_C(0x73f4fae453f9c832))valid=0;
        if(!valid){fprintf(stderr,"Native bootstrap failed: input=%u status=%d hash=%016llx output=%s\n",
            input,status,(unsigned long long)hash,output?output:"(none)");SDL_free(output);return 1;}
        printf("Original executable bootstrap: input=%u hash=%016llx, bounded headless main and real CUE/VRAM passed\n",input,(unsigned long long)hash);
        SDL_free(output);
    }
    return 0;
}

/* Run in its own process: removing an equipped donor must scan empty slots
 * before finding the fallback club, or return -1 when no club remains. */
