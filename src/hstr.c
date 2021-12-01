/*
 hstr.c     HSTR shell history completion utility

 Copyright (C) 2014-2020 Martin Dvorak <martin.dvorak@mindforger.com>

 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
*/

#define _GNU_SOURCE

#include "include/hstr.h"
// 유닉스시간 변환을 위해
#include <time.h>
// atoi사용을 위해
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>

#define SELECTION_CURSOR_IN_PROMPT -1
#define SELECTION_PREFIX_MAX_LNG 512
#define CMDLINE_LNG 2048
#define HOSTNAME_BUFFER 128

#define PG_JUMP_SIZE 10

#define K_CTRL_A 1
#define K_CTRL_E 5
#define K_CTRL_F 6
#define K_CTRL_G 7
#define K_CTRL_H 8
#define K_CTRL_L 12
#define K_CTRL_J 10
#define K_CTRL_K 11

#define K_CTRL_N 14
#define K_CTRL_P 16

#define K_CTRL_R 18
#define K_CTRL_T 20
#define K_CTRL_U 21
#define K_CTRL_W 23
#define K_CTRL_X 24
#define K_CTRL_Z 26

#define K_CTRL_SLASH 31
//하위 디렉토리 탐색  출력 키 컨트롤 D
#define K_CRTL_D  4
#define K_CTRL_B  2

#define K_ESC 27
#define K_TAB 9
#define K_BACKSPACE 127
#define K_ENTER 13

#define HSTR_THEME_MONO   0
#define HSTR_THEME_COLOR  1<<7
#define HSTR_THEME_LIGHT  1|HSTR_THEME_COLOR
#define HSTR_THEME_DARK   2|HSTR_THEME_COLOR

#define HSTR_COLOR_NORMAL  1
#define HSTR_COLOR_HIROW   2
#define HSTR_COLOR_INFO    2
#define HSTR_COLOR_PROMPT  3
#define HSTR_COLOR_DELETE  4
#define HSTR_COLOR_MATCH   5

#define HSTR_ENV_VAR_CONFIG      "HSTR_CONFIG"
#define HSTR_ENV_VAR_PROMPT      "HSTR_PROMPT"
#define HSTR_ENV_VAR_IS_SUBSHELL "HSTR_IS_SUBSHELL"

#define HSTR_CONFIG_THEME_MONOCHROMATIC     "monochromatic"
#define HSTR_CONFIG_THEME_HICOLOR           "hicolor"
#define HSTR_CONFIG_STATIC_FAVORITES        "static-favorites"
#define HSTR_CONFIG_SKIP_FAVORITES_COMMENTS "skip-favorites-comments"
#define HSTR_CONFIG_FAVORITES               "favorites-view"
#define HSTR_CONFIG_SORTING                 "raw-history-view"
#define HSTR_CONFIG_CASE                    "case-sensitive"
#define HSTR_CONFIG_REGEXP                  "regexp-matching"
#define HSTR_CONFIG_SUBSTRING               "substring-matching"
#define HSTR_CONFIG_KEYWORDS                "keywords-matching"
#define HSTR_CONFIG_NO_CONFIRM              "no-confirm"
#define HSTR_CONFIG_VERBOSE_KILL            "verbose-kill"
#define HSTR_CONFIG_PROMPT_BOTTOM           "prompt-bottom"
#define HSTR_CONFIG_HELP_ON_OPPOSITE_SIDE   "help-on-opposite-side"
#define HSTR_CONFIG_HIDE_BASIC_HELP         "hide-basic-help"
#define HSTR_CONFIG_HIDE_HELP               "hide-help"
#define HSTR_CONFIG_BLACKLIST               "blacklist"
#define HSTR_CONFIG_KEEP_PAGE               "keep-page"
#define HSTR_CONFIG_DEBUG                   "debug"
#define HSTR_CONFIG_WARN                    "warning"
#define HSTR_CONFIG_BIG_KEYS_SKIP           "big-keys-skip"
#define HSTR_CONFIG_BIG_KEYS_FLOOR          "big-keys-floor"
#define HSTR_CONFIG_BIG_KEYS_EXIT           "big-keys-exit"
#define HSTR_CONFIG_DUPLICATES              "duplicates"

#define HSTR_DEBUG_LEVEL_NONE  0
#define HSTR_DEBUG_LEVEL_WARN  1
#define HSTR_DEBUG_LEVEL_DEBUG 2

#define HSTR_VIEW_RANKING      0
#define HSTR_VIEW_HISTORY      1
#define HSTR_VIEW_FAVORITES    2
#define HSTR_VIEW_TEST         3
#define HSTR_VIEW_DATE         4
#define HSTR_VIEW_DIRECTORY    5

#define HSTR_MATCH_SUBSTRING   0
#define HSTR_MATCH_REGEXP      1
#define HSTR_MATCH_KEYWORDS    2

#define HSTR_NUM_HISTORY_MATCH 3

#define HSTR_CASE_INSENSITIVE  0
#define HSTR_CASE_SENSITIVE    1

#ifdef DEBUG_KEYS
#define LOGKEYS(Y,KEY) mvprintw(Y, 0, "Key: '%3d' / Char: '%c'", KEY, KEY); clrtoeol()
#else
#define LOGKEYS(Y,KEY)
#endif

#ifdef DEBUG_CURPOS
#define LOGCURSOR(Y) mvprintw(Y, 0, "X/Y: %3d / %3d", getcurx(stdscr), getcury(stdscr))
#else
#define LOGCURSOR(Y)
#endif

#ifdef DEBUG_UTF8
#define LOGUTF8(Y,P) mvprintw(Y, 0, "strlen() %zd, mbstowcs() %zd, hstr_strlen() %d",strlen(P),mbstowcs(NULL,P,0),hstr_strlen(P)); clrtoeol()
#else
#define LOGUTF8(Y,P)
#endif

#ifdef DEBUG_SELECTION
#define LOGSELECTION(Y,SCREEN,MODEL) mvprintw(Y, 0, "Selection: screen %3d, model %3d", SCREEN, MODEL); clrtoeol()
#else
#define LOGSELECTION(Y,SCREEN,MODEL)
#endif

// major.minor.revision
static const char* VERSION_STRING=
        "hstr version \"2.3.0\" (2020-11-19T07:41:00)"
        "\n";

// hstr 설명문 컨트롤 + / view 목록  
// 기본 명령어 추가 Base_Command ;
static const char* HSTR_VIEW_LABELS[]={
        "ranking",
        "history",
        "favorites",
        "Base_Command",
        "Time_Line",
        "directory"
};

static const char* HSTR_MATCH_LABELS[]={
        "exact",
        "regexp",
        "keywords"
};

static const char* HSTR_CASE_LABELS[]={
        "insensitive",
        "sensitive"
};

static const char* INSTALL_BASH_STRING=
        "\n# HSTR configuration - add this to ~/.bashrc"
        "\nalias hh=hstr                    # hh to be alias for hstr"
        "\nexport HSTR_CONFIG=hicolor       # get more colors"
        "\nshopt -s histappend              # append new history items to .bash_history"
        "\nexport HISTCONTROL=ignorespace   # leading space hides commands from history"
        "\nexport HISTFILESIZE=10000        # increase history file size (default is 500)"
        "\nexport HISTSIZE=${HISTFILESIZE}  # increase history size (default is 500)"
        // PROMPT_COMMAND considerations:
        //   history -a ... append NEW entries from memory to .bash_history (i.e. flush to file where HSTR reads commands)
        //   history -n ... append NEW entries from .bash_history to memory i.e. NOT entire history reload
        //   history -c ... CLEAR in memory history (keeps .bash_history content)
        //   history -r ... append ALL entries from .bash_history to memory (useful to sync DIFFERENT bash sessions)
        // Conclusion:
        //   -a -n ... Fastest and almost-consistent option i.e. there is efficiency/integrity trade-off.
        //             It works correctly if memory entries are not deleted by HSTR. It doesn't synchronize history
        //             across different bash sessions.
        //   -c -r ... Forces entire .bash_history to be reloaded (handles history deletes, synchronizes different bash sessions)
        "\n# ensure synchronization between bash memory and history file"
        "\nexport PROMPT_COMMAND=\"history -a; history -n; ${PROMPT_COMMAND}\""
        "\n# if this is interactive shell, then bind hstr to Ctrl-r (for Vi mode check doc)"
#if defined(__MS_WSL__)
        // IMPROVE commands are NOT executed on return under win10 > consider hstr_utils changes
        // Script hints:
        //  {...} is inline group ~ lambda function whose vars are visible to the other commands
        //   V=$(c) executes commands and stores it to var V
        "\nfunction hstrwsl {"
        "\n  offset=${READLINE_POINT}"
        "\n  READLINE_POINT=0"
        "\n  { READLINE_LINE=$(</dev/tty hstr ${READLINE_LINE:0:offset} 2>&1 1>&$hstrout); } {hstrout}>&1"
        "\n  READLINE_POINT=${#READLINE_LINE}"
        "\n}"
        "\nif [[ $- =~ .*i.* ]]; then bind -x '\"\\C-r\": \"hstrwsl\"'; fi"
#elif defined(__CYGWIN__)
        "\nfunction hstrcygwin {"
        "\n  offset=${READLINE_POINT}"
        "\n  READLINE_POINT=0"
        "\n  { READLINE_LINE=$(</dev/tty hstr ${READLINE_LINE:0:offset} 2>&1 1>&$hstrout); } {hstrout}>&1"
        "\n  READLINE_POINT=${#READLINE_LINE}"
        "\n}"
        "\nif [[ $- =~ .*i.* ]]; then bind -x '\"\\C-r\": \"hstrcygwin\"'; fi"
#else
        "\nif [[ $- =~ .*i.* ]]; then bind '\"\\C-r\": \"\\C-a hstr -- \\C-j\"'; fi"
        "\n# if this is interactive shell, then bind 'kill last command' to Ctrl-x k"
        "\nif [[ $- =~ .*i.* ]]; then bind '\"\\C-xk\": \"\\C-a hstr -k \\C-j\"'; fi"
#endif
        "\n\n";

// zsh doc: http://zsh.sourceforge.net/Guide/zshguide.html
static const char* INSTALL_ZSH_STRING=
        "\n# HSTR configuration - add this to ~/.zshrc"
        "\nalias hh=hstr                    # hh to be alias for hstr"
        "\nsetopt histignorespace           # skip cmds w/ leading space from history"
        // HISTFILE should not be needed - HSTR must work on blank environment as well
        // "\nexport HISTFILE=~/.zsh_history   # ensure history file visibility"
        "\nexport HSTR_CONFIG=hicolor       # get more colors"
#if defined(__MS_WSL__)
        // TODO binding to be rewritten for zsh@WSL as it's done for bash - hstr_winwsl() like function to be implemented to make it work on WSL

        "\n# Function and binding below is bash script that makes command completion work under WSL."
        "\n# If you can rewrite the function and binding from bash to zsh please send it to martin.dvorak@mindforger.com"
        "\n# so that I can share it with other users."
        "\n#function hstr_winwsl {"
        "\n#  offset=${READLINE_POINT}"
        "\n#  READLINE_POINT=0"
        "\n#  { READLINE_LINE=$(</dev/tty hstr ${READLINE_LINE:0:offset} 2>&1 1>&$hstrout); } {hstrout}>&1"
        "\n#  READLINE_POINT=${#READLINE_LINE}"
        "\n#}"
        "\n#bindkey -s \"\\C-r\" \"\\eqhstr_winwsl\\n\""
        "\n"
        "\nbindkey -s \"\\C-r\" \"\\eqhstr\\n\"     # bind hstr to Ctrl-r (for Vi mode check doc)"
#elif defined(__CYGWIN__)
        // TODO binding to be rewritten for zsh@Cygwin as it's done for bash - hstr_cygwin() like function to be implemented to make it work under Cygwin

        "\n# Function and binding below is bash script that makes command completion work under Cygwin."
        "\n# If you can rewrite the function and binding from bash to zsh please send it to martin.dvorak@mindforger.com"
        "\n# so that I can share it with other users."
        "\n#function hstr_cygwin {"
        "\n#  offset=${READLINE_POINT}"
        "\n#  READLINE_POINT=0"
        "\n#  { READLINE_LINE=$(</dev/tty hstr ${READLINE_LINE:0:offset} 2>&1 1>&$hstrout); } {hstrout}>&1"
        "\n#  READLINE_POINT=${#READLINE_LINE}"
        "\n#}"
        "\n#bindkey -s \"\\C-r\" \"\\eqhstr_cygwin\\n\""
        "\n"
        "\nbindkey -s \"\\C-r\" \"\\eqhstr\\n\"     # bind hstr to Ctrl-r (for Vi mode check doc)"
#else
        "\nbindkey -s \"\\C-r\" \"\\C-a hstr -- \\C-j\"     # bind hstr to Ctrl-r (for Vi mode check doc)"
#endif
        // TODO try variant with args/pars separation
        //"\nbindkey -s \"\\C-r\" \"\\eqhstr --\\n\"     # bind hstr to Ctrl-r (for Vi mode check doc)"
        // alternate binding options in zsh:
        //   bindkey -s '^R' '^Ahstr ^M'
        //   bindkey -s "\C-r" "\C-a hstr -- \C-j"
        "\n\n";

static const char* HELP_STRING=
        "Usage: hstr [option] [arg1] [arg2]..."
        "\nShell history suggest box:"
        "\n"
        "\n  --favorites              -f ... show favorites view"
        "\n  --kill-last-command      -k ... delete last command in history"
        "\n  --non-interactive        -n ... print filtered history and exit"
        "\n  --show-configuration     -s ... show configuration to be added to ~/.bashrc"
        "\n  --show-zsh-configuration -z ... show zsh configuration to be added to ~/.zshrc"
        "\n  --show-blacklist         -b ... show commands to skip on history indexation"
        "\n  --version                -V ... show version details"
        "\n  --help                   -h ... help"
        "\n"
        "\nReport bugs to martin.dvorak@mindforger.com"
        "\nHome page: https://github.com/dvorka/hstr"
        "\n";

// TODO help screen - curses window (tig)
static const char* LABEL_HELP=
        "Type to filter, UP/DOWN move, RET/TAB select, DEL remove, C-f add favorite, C-g cancel C-h dir c-b tag";

#define GETOPT_NO_ARGUMENT           0
#define GETOPT_REQUIRED_ARGUMENT     1
#define GETOPT_OPTIONAL_ARGUMENT     2

static const struct option long_options[] = {
        {"favorites",              GETOPT_NO_ARGUMENT, NULL, 'f'},
        {"kill-last-command",      GETOPT_NO_ARGUMENT, NULL, 'k'},
        {"version",                GETOPT_NO_ARGUMENT, NULL, 'V'},
        {"help",                   GETOPT_NO_ARGUMENT, NULL, 'h'},
        {"non-interactive",        GETOPT_NO_ARGUMENT, NULL, 'n'},
        {"show-configuration",     GETOPT_NO_ARGUMENT, NULL, 's'},
        {"show-zsh-configuration", GETOPT_NO_ARGUMENT, NULL, 'z'},
        {"show-blacklist",         GETOPT_NO_ARGUMENT, NULL, 'b'},
        {0,                        0,                  NULL,  0 }
};

// 기본 명령어 파일 저장 이름
#define FILE_HSTR_MYCOMMANDITEM ".hstr_mycommand"
// 히스토리 저장파일(일단 그대로 가져다씀) 
#define FILE_HSTR_DATEITEM ".bash_history"

// 기본 명령어 보기 테스트 구조체  Favorite 구조체 따라함
typedef struct MyCommandItem 
{
    char** items;
    unsigned count;
    bool loaded;
    bool reorderOnChoice;
    bool skipComments;
    HashSet* set;
} MyCommandItem;
// 기본 명령어 보기 구조체 선언
static MyCommandItem* mycommandtest;

// 하위 디렉토리 구조체
typedef struct DirItem
{
    char ** items;
    unsigned count;
    bool loaded;
    bool reorderOnChoice;
    bool skipComments;
    HashSet* set;
} DirItem;
// 하위 디렉토리 구조체 선언
static DirItem* diritem;


//hstr 구조체
typedef struct {
    HistoryItems* history;      //hstr_history.h 에 선언
    FavoriteItems* favorites;
    Blacklist blacklist;
    HstrRegexp regexp;

    char cmdline[CMDLINE_LNG];

    char **selection;
    unsigned selectionSize;
    regmatch_t *selectionRegexpMatch;

    bool interactive;

    int matching;
    int view;
    int caseSensitive;

    unsigned char theme;
    bool noRawHistoryDuplicates;
    bool keepPage; // do NOT clear page w/ selection on HSTR exit
    bool noConfirm; // do NOT ask for confirmation on history entry delete
    bool verboseKill; // write a message on delete of the last command in history
    int bigKeys;
    int debugLevel;
    bool promptBottom;
    bool helpOnOppositeSide;
    bool hideBasicHelp;
    bool hideHistoryHelp;

    int promptY;
    int promptYHelp;
    int promptYHistory;
    int promptYNotification;
    int promptYItemsStart;
    int promptYItemsEnd;
    int promptItems;

    bool noIoctl;
} Hstr;

static Hstr* hstr;

// 기본 명령어 보기 구조체 초기화
void MyCommandItem_init(MyCommandItem* Mycommand)
{
    Mycommand->items=NULL;
    Mycommand->count=0;
    Mycommand->loaded=false;
    Mycommand->reorderOnChoice=true;
    Mycommand->skipComments=false;
    Mycommand->set=malloc(sizeof(HashSet));
    hashset_init(Mycommand->set);
}

//구조체 제거 메모리 제거 
void MyCommandItem_destroy(MyCommandItem* Mycommand)
{
    if(Mycommand) {
        // TODO hashset destroys keys - no need to destroy items!
        unsigned i;
        for(i=0; i<Mycommand->count; i++) {
            free(Mycommand->items[i]);
        }
        free(Mycommand->items);
        hashset_destroy(Mycommand->set, false);
        free(Mycommand->set);
        free(Mycommand);
    }
}
int filecopy(char *exist, char*cpnew){
    FILE *fexist, *fcpnew;
    int a;
    if((fexist = fopen(exist,"rb")) == NULL){
        return -1;
    }
    if((fcpnew = fopen(cpnew,"wb")) == NULL){
        fclose(fexist);
        return -1;
    }
    while (1)
    {
        a = fgetc(fexist);
        if(!feof(fexist)){
            fputc(a,fcpnew);
        }else{
            break;
        }
    }
    fclose(fexist);
    fclose(fcpnew);
    return 0;
}

// 기본 명령어 보기 구조체 파일 이름 반환
char* MyCommandItem_get_filename()
{
    char* home = getenv(ENV_VAR_HOME);
    char* fileName = (char*) malloc(strlen(home) + 1 + strlen(FILE_HSTR_MYCOMMANDITEM) + 1);
    strcpy(fileName, home);
    strcat(fileName, "/");
    strcat(fileName, FILE_HSTR_MYCOMMANDITEM);
    FILE *file;
    if(file = fopen(fileName,"r")){
        fclose(file);
    }else{
        fclose(file);
        filecopy("../.hstr_mycommand",fileName);
    }
    return fileName;
}

// 기본 명령어 저장된 파일 읽기
void MyCommandItem_get(MyCommandItem* Mycommand)
{
    if(!Mycommand->loaded) {
        char* fileName = MyCommandItem_get_filename();
        char* fileContent = NULL;
        if(access(fileName, F_OK) != -1) {
            long inputFileSize;

            FILE* inputFile = fopen(fileName, "rb");
            fseek(inputFile, 0, SEEK_END);
            inputFileSize = ftell(inputFile);
            rewind(inputFile);
            fileContent = malloc((inputFileSize + 1) * (sizeof(char)));
            if(!fread(fileContent, sizeof(char), inputFileSize, inputFile)) {
                if(ferror(inputFile)) {
                    exit(EXIT_FAILURE);
                }
            }
            fclose(inputFile);
            fileContent[inputFileSize] = 0;

            if(fileContent && strlen(fileContent)) {
                Mycommand->count = 0;
                char* p=strchr(fileContent,'\n');
                while (p!=NULL) {
                    Mycommand->count++;
                    p=strchr(p+1,'\n');
                }

                Mycommand->items = malloc(sizeof(char*) * Mycommand->count);
                Mycommand->count = 0;
                char* pb=fileContent, *pe, *s;
                pe=strchr(fileContent, '\n');
                while(pe!=NULL) {
                    *pe=0;
                    if(!hashset_contains(Mycommand->set,pb)) {
                        if(!Mycommand->skipComments || !(strlen(pb) && pb[0]=='#')) {
                            s=hstr_strdup(pb);
                            Mycommand->items[Mycommand->count++]=s;
                            hashset_add(Mycommand->set,s);
                        }
                    }
                    pb=pe+1;
                    pe=strchr(pb, '\n');
                }
                free(fileContent);
            }
        } else {
            Mycommand->loaded=true;
        }
        free(fileName);
    }
}

//하위 디렉토리 구조체 초기화
void DirItem_init()
{
    diritem->items=NULL;
    diritem->count=0;
    diritem->loaded=false;
    diritem->reorderOnChoice=true;
    diritem->skipComments=false;
    diritem->set=malloc(sizeof(HashSet));
    hashset_init(diritem->set);
}
// 하위 디렉토리 구조체 메모리 해제
void DirItem_destroy(DirItem* dir)
{
    if(dir) {
        // TODO hashset destroys keys - no need to destroy items!
        unsigned i;
        for(i=0; i<dir->count; i++) {
            free(dir->items[i]);
        }
        free(dir->items);
        hashset_destroy(dir->set, false);
        free(dir->set);
        free(dir);
    }
}

// 파일 목록 필터링
static int file_filter(const struct dirent *entry){
    if(strcmp(entry->d_name,"..") == 0){
        return 0;
    }
    if(strcmp(entry->d_name,".") == 0){
        return 0;
    }
    //stat 구조체 파일 정보
    struct stat st;
    // stat 함수 경로/파일 의 파일 정보 st에 저장
    stat(entry->d_name,&st);
    // st_mode 로 디렉토리 인지 아닌지 판별
    if (st.st_mode & S_IFDIR){
        return 1;
    }
    
    // 0이면 출력
    return 0;
}

// 하위 디렉토리 탐색 목록 얻기
void DirItem_get(){
    if(!diritem->loaded){
        int filecount;
        int buffer = 2048;
        char path[buffer];
        struct dirent **dirlist;
        // 현재 디렉토리 위치
        getcwd(path,buffer);
        // 디렉토리 탐색,
        filecount = scandir(path, &dirlist, file_filter, NULL);

        if(filecount == -1){
            printf("%s 디렉토리 스캔 에러 \n",path);
        }
        // 스캔 내용 문자열에 넣기
        char namelist[255 * filecount];
        namelist[0] = 0;
        while(filecount--){
            strcat(namelist,"cd ");
            strcat(namelist,dirlist[filecount]->d_name);
            strcat(namelist,"\n");
            free(dirlist[filecount]);
        }
        free(dirlist);
        //리스트 마지막 \n 제거
        //디렉토리 탐색 결과 비교
        if(namelist && strlen(namelist)){
            diritem->count = 0;
            char* p = strchr(namelist,'\n');
            while (p != NULL){
                diritem->count++;
                p = strchr(p+1,'\n');
            }

            diritem->items = malloc(sizeof(char*) * diritem->count);
            diritem->count = 0;
            char* pb = namelist, *pe, *s;
            pe = strchr(namelist, '\n');
            while (pe != NULL)
            {
                *pe = 0;
                if(!hashset_contains(diritem->set,pb)){
                    if(!diritem->skipComments || !(strlen(pb) && pb[0] == '#')){
                        s = hstr_strdup(pb);
                        diritem->items[diritem->count++]=s;
                        hashset_add(diritem->set,s);
                    }
                }
                pb = pe + 1;
                pe = strchr(pb, '\n');
            }
        }
    }
}

// 날짜표시 구조체 선언
typedef struct DateItem 
{
    char** items;
    unsigned count;
    bool loaded;
    bool reorderOnChoice;
    bool skipComments;
    HashSet* set;
} DateItem;
static DateItem* dateitem;


// 날짜표시 구조체 초기화
void DateItem_init(DateItem* Dateitem)
{
    Dateitem->items=NULL;
    Dateitem->count=0;
    Dateitem->loaded=false;
    Dateitem->reorderOnChoice=true;
    Dateitem->skipComments=false;
    Dateitem->set=malloc(sizeof(HashSet));
    hashset_init(Dateitem->set);
}

// 날짜표시 메모리 제거 
void DateItem_destroy(DateItem* Dateitem)
{
    if(Dateitem) {
        // TODO hashset destroys keys - no need to destroy items!
        unsigned i;
        for(i=0; i<Dateitem->count; i++) {
            free(Dateitem->items[i]);
        }
        free(Dateitem->items);
        hashset_destroy(Dateitem->set, false);
        free(Dateitem->set);
        free(Dateitem);
    }
}

// 날짜표시 구조체 파일 이름 반환
char* DateItem_get_filename()
{
    char* home = getenv(ENV_VAR_HOME);
    char* fileName = (char*) malloc(strlen(home) + 1 + strlen(FILE_HSTR_DATEITEM) + 1);
    strcpy(fileName, home);
    strcat(fileName, "/");
    strcat(fileName, FILE_HSTR_DATEITEM);
    return fileName;
}

// 히스토리 파일 읽기
void DateItem_get(DateItem* Dateitem)
{
    if(!Dateitem->loaded) {
        char* fileName = DateItem_get_filename();
        // 실제 내용을 저장할 변수
        if(access(fileName, F_OK) != -1) {
            long inputFileSize;
            // get_filename으로 받은 파일경로를 오픈하여 inputFile에 저장
            FILE* inputFile = fopen(fileName, "rb");
            // 파일의 rw위치를 마지막으로 옮기고, 그 위치를 inputFileSize에 저장
            fseek(inputFile, 0, SEEK_END);
            inputFileSize = ftell(inputFile);
            char fileContent[inputFileSize];
            char realfileContent[inputFileSize];
            // 위치를 다시 맨앞으로 되돌림
            rewind(inputFile);
            // inputFile을 한char씩 FileSize만큼 읽어 fileContent에 저장
            if(!fread(fileContent, sizeof(char), inputFileSize, inputFile)) {
                // 읽기 오류테스트
                if(ferror(inputFile)) {
                    exit(EXIT_FAILURE);
                }
            }
            // fileContent에 내용 저장했으니 inputFile은 닫고, fileContent의 말미에 0저장
            fclose(inputFile);
            fileContent[inputFileSize] = 0;

            if(fileContent && strlen(fileContent)) {
                Dateitem->count = 0;
                // 줄바꿈이 있는지 검사하여 해당 포인터를 반환.
                char* p=strchr(fileContent,'\n');
                // 몇줄이 있는지 검사하여 Dateitem.count에 저장
                while (p!=NULL) {
                    Dateitem->count++;
                    p=strchr(p+1,'\n');
                }
                // Dateitem.items에 char*줄 수만큼 메모리 할당하고 카운트 다시 초기화
                Dateitem->items = malloc(sizeof(char*) * Dateitem->count);
                Dateitem->count = 0;
                
                time_t timer;
                struct tm *t;

                char* q = strtok(fileContent, "\n");
                char st[20] = {0x00};
                while (q != NULL) {
                    if(q[0] == '#'){
                        strcpy(q, q+1);
                        timer = atoi(q);
                        t = localtime(&timer);
                        sprintf(st, "%d/%d  ", t->tm_mon+1, t->tm_mday);
                        strcat(realfileContent, st);
                        q = strtok(NULL, "\n");
                        strcat(realfileContent, q);
                        strcat(realfileContent, "\n");
                    }
                    q = strtok(NULL, "\n");
                }

                char* pb=realfileContent, *pe, *s;
                // 첫줄 끝의 포인터를 pe에 저장. pe는 다음줄이 있는지 검사하는 변수
                pe=strchr(realfileContent, '\n');
                // 줄바꿈이 있는 동안 반복
                while(pe!=NULL) {
                    *pe=0;
                    if(!hashset_contains(Dateitem->set,pb)) {
                        if(!Dateitem->skipComments || !(strlen(pb) && pb[0]=='#')) {
                            s=hstr_strdup(pb);
                            Dateitem->items[Dateitem->count++]=s;
                            hashset_add(Dateitem->set,s);
                        }
                    }
                    pb=pe+1;
                    pe=strchr(pb, '\n');
                }
            }
        } else {
            // favorites file not found > favorites don't exist yet
            Dateitem->loaded=true;
        }
        free(fileName);
    }
}

// 시작시 처음 초기화
void hstr_init(void)
{
    hstr->history=NULL;
    hstr->favorites=malloc(sizeof(FavoriteItems));
    // 기본명령어 하위 디렉토리 초기화
    MyCommandItem_init(mycommandtest);
    DirItem_init();
    // 날짜 정렬 구조체 초기화
    DateItem_init(dateitem);
    favorites_init(hstr->favorites);
    blacklist_init(&hstr->blacklist);
    hstr_regexp_init(&hstr->regexp);

    hstr->cmdline[0]=0;

    hstr->selection=NULL;
    hstr->selectionRegexpMatch=NULL;
    hstr->selectionSize=0;

    hstr->interactive=true;

    hstr->matching=HSTR_MATCH_KEYWORDS;
    hstr->view=HSTR_VIEW_RANKING;
    hstr->caseSensitive=HSTR_CASE_INSENSITIVE;

    hstr->theme=HSTR_THEME_MONO;
    hstr->noRawHistoryDuplicates=true;
    hstr->keepPage=false;
    hstr->noConfirm=false;
    hstr->verboseKill=false;
    hstr->bigKeys=RADIX_BIG_KEYS_SKIP;
    hstr->debugLevel=HSTR_DEBUG_LEVEL_NONE;
    hstr->promptBottom=false;
    hstr->helpOnOppositeSide=false;
    hstr->hideBasicHelp=false;
    hstr->hideHistoryHelp=false;

    hstr->promptY
     =hstr->promptYHelp
     =hstr->promptYHistory
     =hstr->promptYNotification
     =hstr->promptYItemsStart
     =hstr->promptYItemsEnd
     =hstr->promptItems
     =0;

    hstr->noIoctl=false;
}

// 메모리 할당 종료
void hstr_destroy(void)
{
    //기본 명령어 할당 종료
    MyCommandItem_destroy(mycommandtest);
    //하위 디렉토리 메모리 해제
    DirItem_destroy(diritem);
    //날짜표시 메모리종료
    DateItem_destroy(dateitem);
    favorites_destroy(hstr->favorites);
    hstr_regexp_destroy(&hstr->regexp);
    // blacklist is allocated by hstr struct
    blacklist_destroy(&hstr->blacklist, false);
    prioritized_history_destroy(hstr->history);
    if(hstr->selection) free(hstr->selection);
    if(hstr->selectionRegexpMatch) free(hstr->selectionRegexpMatch);
    free(hstr);
}

void hstr_exit(int status)
{
    hstr_destroy();
    exit(status);
}

void signal_callback_handler_ctrl_c(int signum)
{
    if(signum==SIGINT) {
        history_mgmt_flush();
        hstr_curses_stop(false);
        hstr_exit(signum);
    }
}

unsigned recalculate_max_history_items(void)
{
    // 창에서 Y 값 N으로
    int n = getmaxy(stdscr);
    hstr->promptItems = n-1;   //promptItems 기록 표시 목록 크기
    // 설정에 따라 hstr 표시 결정
    if(!hstr->hideBasicHelp) {
        hstr->promptItems--;
    }
    if(!hstr->hideHistoryHelp) {
        hstr->promptItems--;
    }
    // promptBottom 명령어 입력창 위치 아래인지 아닌지
    if(hstr->promptBottom) {
        // 도움말 표시 위치 
        if(hstr->helpOnOppositeSide) {
            // Layout:
            // - [basic help]
            // - [history help]
            // - items start
            // - ...
            // - items end
            // - prompt
            int top = 0;
            // promptY 입력창 위치
            hstr->promptY = n-1;
            if(!hstr->hideBasicHelp) {
                hstr->promptYHelp = top++;
            }
            if(!hstr->hideHistoryHelp) {
                hstr->promptYHistory = top++;
            }
            hstr->promptYItemsStart = top++;
        } else {
            // Layout:
            // - items start
            // - ...
            // - items end
            // - [history help]
            // - [basic help]
            // - prompt
            int bottom = n-1;
            hstr->promptY = bottom--;
            if(!hstr->hideBasicHelp) {
                hstr->promptYHelp = bottom--;
            }
            if(!hstr->hideHistoryHelp) {
                hstr->promptYHistory = bottom--;
            }
            hstr->promptYItemsStart = 0;
        }
    } else {
        if(hstr->helpOnOppositeSide) {
            // Layout:
            // - prompt
            // - items start
            // - ...
            // - items end
            // - [history help]
            // - [basic help]
            int bottom = n-1;
            hstr->promptY = 0;
            if(!hstr->hideBasicHelp) {
                hstr->promptYHelp = bottom--;
            }
            if(!hstr->hideHistoryHelp) {
                hstr->promptYHistory = bottom--;
            }
            hstr->promptYItemsStart = 1;
        } else {
            // Layout:
            // - prompt
            // - [basic help]
            // - [history help]
            // - items start
            // - ...
            // - items end
            int top = 0;
            hstr->promptY = top++;
            if(!hstr->hideBasicHelp) {
                hstr->promptYHelp = top++;
            }
            if(!hstr->hideHistoryHelp) {
                hstr->promptYHistory = top++;
            }
            hstr->promptYItemsStart = top++;
        }
    }// 출력 모양 결정 완료
    // 기록명령어 표시 끝 위치 결정 : 시작 위치 + 목록 크기 -1
    hstr->promptYItemsEnd = hstr->promptYItemsStart+hstr->promptItems-1;
    if(!hstr->hideBasicHelp) {
        // Use basic help label for notifications.
        hstr->promptYNotification = hstr->promptYHelp;
    }
    else {
        // If basic help is hidden, we need another place to put notifications to.
        if(hstr->hideBasicHelp) {
            if(!hstr->hideHistoryHelp) {
                // Use history help label.
                hstr->promptYHelp = hstr->promptYHistory;
            } else {
                // Use one of the command item lines.
                if((hstr->promptBottom && hstr->helpOnOppositeSide) || (!hstr->promptBottom && !hstr->helpOnOppositeSide)) {
                    hstr->promptYHelp = hstr->promptYItemsStart;
                } else {
                    hstr->promptYHelp = hstr->promptYItemsEnd;
                }
            }
        }
    }
    return hstr->promptItems;
}

void hstr_get_env_configuration()
{
    char *hstr_config=getenv(HSTR_ENV_VAR_CONFIG);
    if(hstr_config && strlen(hstr_config)>0) {
        if(strstr(hstr_config,HSTR_CONFIG_THEME_MONOCHROMATIC)) {
            hstr->theme=HSTR_THEME_MONO;
        } else {
            if(strstr(hstr_config,HSTR_CONFIG_THEME_HICOLOR)) {
                hstr->theme=HSTR_THEME_DARK;
            }
        }
        if(strstr(hstr_config,HSTR_CONFIG_CASE)) {
            hstr->caseSensitive=HSTR_CASE_SENSITIVE;
        }
        if(strstr(hstr_config,HSTR_CONFIG_REGEXP)) {
            hstr->matching=HSTR_MATCH_REGEXP;
        } else {
            if(strstr(hstr_config,HSTR_CONFIG_SUBSTRING)) {
                hstr->matching=HSTR_MATCH_SUBSTRING;
            } else {
                if(strstr(hstr_config,HSTR_CONFIG_KEYWORDS)) {
                    hstr->matching=HSTR_MATCH_KEYWORDS;
                }
            }
        }
        if(strstr(hstr_config,HSTR_CONFIG_SORTING)) {
            hstr->view=HSTR_VIEW_HISTORY;
        } else {
            if(strstr(hstr_config,HSTR_CONFIG_FAVORITES)) {
                hstr->view=HSTR_VIEW_FAVORITES;
            }
        }
        if(strstr(hstr_config,HSTR_CONFIG_BIG_KEYS_EXIT)) {
            hstr->bigKeys=RADIX_BIG_KEYS_EXIT;
        } else {
            if(strstr(hstr_config,HSTR_CONFIG_BIG_KEYS_FLOOR)) {
                hstr->bigKeys=RADIX_BIG_KEYS_FLOOR;
            } else {
                hstr->bigKeys=RADIX_BIG_KEYS_SKIP;
            }
        }
        if(strstr(hstr_config,HSTR_CONFIG_VERBOSE_KILL)) {
            hstr->verboseKill=true;
        }
        if(strstr(hstr_config,HSTR_CONFIG_BLACKLIST)) {
            hstr->blacklist.useFile=true;
        }
        if(strstr(hstr_config,HSTR_CONFIG_KEEP_PAGE)) {
            hstr->keepPage=true;
        }
        if(strstr(hstr_config,HSTR_CONFIG_NO_CONFIRM)) {
            hstr->noConfirm=true;
        }
        if(strstr(hstr_config,HSTR_CONFIG_STATIC_FAVORITES)) {
            hstr->favorites->reorderOnChoice=false;
        }
        if(strstr(hstr_config,HSTR_CONFIG_SKIP_FAVORITES_COMMENTS)) {
            hstr->favorites->skipComments=true;
        }

        if(strstr(hstr_config,HSTR_CONFIG_DEBUG)) {
            hstr->debugLevel=HSTR_DEBUG_LEVEL_DEBUG;
        } else {
            if(strstr(hstr_config,HSTR_CONFIG_WARN)) {
                hstr->debugLevel=HSTR_DEBUG_LEVEL_WARN;
            }
        }

        if(strstr(hstr_config,HSTR_CONFIG_DUPLICATES)) {
            hstr->noRawHistoryDuplicates=false;
        }

        if(strstr(hstr_config,HSTR_CONFIG_PROMPT_BOTTOM)) {
            hstr->promptBottom = true;
        } else {
            hstr->promptBottom = false;
        }
        if(strstr(hstr_config,HSTR_CONFIG_HELP_ON_OPPOSITE_SIDE)) {
            hstr->helpOnOppositeSide = true;
        } else {
            hstr->helpOnOppositeSide = false;
        }
        if(strstr(hstr_config,HSTR_CONFIG_HIDE_HELP)) {
            hstr->hideBasicHelp = true;
            hstr->hideHistoryHelp = true;
        } else {
            if(strstr(hstr_config,HSTR_CONFIG_HIDE_BASIC_HELP)) {
                hstr->hideBasicHelp = true;
                hstr->hideHistoryHelp = false;
            } else {
                hstr->hideBasicHelp = false;
                hstr->hideHistoryHelp = false;
            }
        }
        recalculate_max_history_items();
    }
}

// 명령어 입력창 표시 출력  user@호스트 이름 길이 반환
unsigned print_prompt(void)
{
    unsigned xoffset = 0, promptLength;

    if(hstr->theme & HSTR_THEME_COLOR) {
        color_attr_on(COLOR_PAIR(HSTR_COLOR_PROMPT));
        color_attr_on(A_BOLD);
    }
    // getenv 환경 변수 검색 값 반환
    char *prompt = getenv(HSTR_ENV_VAR_PROMPT);
    if(prompt) {
        mvprintw(hstr->promptY, xoffset, "%s", prompt);
        promptLength=strlen(prompt);
    } else {
        char *user = getenv(ENV_VAR_USER);
        char *hostname=malloc(HOSTNAME_BUFFER);
        user=(user?user:"me");
        get_hostname(HOSTNAME_BUFFER, hostname);
        // 입력창에 user@호스트 이름 표시 $
        mvprintw(hstr->promptY, xoffset, "%s@%s$ ", user, hostname);
        // 입력창 계정 정보 길이 뒤에 +2 위치 길이 반환
        promptLength=strlen(user)+1+strlen(hostname)+1+1;
        free(hostname);
    }

    if(hstr->theme & HSTR_THEME_COLOR) {
        color_attr_off(A_BOLD);
        color_attr_off(COLOR_PAIR(HSTR_COLOR_PROMPT));
    }
    refresh();

    return promptLength;
}

void add_to_selection(char* line, unsigned int* index)
{
    if(hstr->noRawHistoryDuplicates) {
        unsigned i;
        for(i = 0; i < *index; i++) {
            if (strcmp(hstr->selection[i], line) == 0) {
                return;
            }
        }
    }
    hstr->selection[*index]=line;
    (*index)++;
}

void print_help_label(void)
{
    if(hstr->hideBasicHelp)
        return;

    int cursorX=getcurx(stdscr);  //getcurx x는 열, y는 줄 커서 위치
    int cursorY=getcury(stdscr);

    char screenLine[CMDLINE_LNG];
    // hstr 실행 2번줄 도움말  promptYHelp hstr 모드시 줄 위치 추정
    snprintf(screenLine, getmaxx(stdscr), "%s", LABEL_HELP);
    // clrtoeol 커서 오른쪽 모두 지움
    //mvprintw  (Y,X,내용)  Y : 줄 위치 ,X 열 위치 promptYHelp 도움말 위치
    mvprintw(hstr->promptYHelp, 0, "%s", screenLine); clrtoeol();
    refresh();

    move(cursorY, cursorX);
}

void print_confirm_delete(const char* cmd)
{
    char screenLine[CMDLINE_LNG];
    if(hstr->view==HSTR_VIEW_FAVORITES) {
        snprintf(screenLine, getmaxx(stdscr), "Do you want to delete favorites item '%s'? y/n", cmd);
    } else {
        snprintf(screenLine, getmaxx(stdscr), "Do you want to delete all occurrences of '%s'? y/n", cmd);
    }
    // TODO make this function
    if(hstr->theme & HSTR_THEME_COLOR) {
        color_attr_on(COLOR_PAIR(HSTR_COLOR_DELETE));
        color_attr_on(A_BOLD);
    }
    mvprintw(hstr->promptYNotification, 0, "%s", screenLine);
    if(hstr->theme & HSTR_THEME_COLOR) {
        color_attr_off(A_BOLD);
        color_attr_on(COLOR_PAIR(HSTR_COLOR_NORMAL));
    }
    clrtoeol();
    refresh();
}

void print_cmd_deleted_label(const char* cmd, int occurences)
{
    char screenLine[CMDLINE_LNG];
    if(hstr->view==HSTR_VIEW_FAVORITES) {
        snprintf(screenLine, getmaxx(stdscr), "Favorites item '%s' deleted", cmd);
    } else {
        snprintf(screenLine, getmaxx(stdscr), "History item '%s' deleted (%d occurrence%s)", cmd, occurences, (occurences==1?"":"s"));
    }
    // TODO make this function
    if(hstr->theme & HSTR_THEME_COLOR) {
        color_attr_on(COLOR_PAIR(HSTR_COLOR_DELETE));
        color_attr_on(A_BOLD);
    }
    mvprintw(hstr->promptYNotification, 0, "%s", screenLine);
    if(hstr->theme & HSTR_THEME_COLOR) {
        color_attr_off(A_BOLD);
        color_attr_on(COLOR_PAIR(HSTR_COLOR_NORMAL));
    }
    clrtoeol();
    refresh();
}

void print_regexp_error(const char* errorMessage)
{
    char screenLine[CMDLINE_LNG];
    snprintf(screenLine, getmaxx(stdscr), "%s", errorMessage);
    if(hstr->theme & HSTR_THEME_COLOR) {
        color_attr_on(COLOR_PAIR(HSTR_COLOR_DELETE));
        color_attr_on(A_BOLD);
    }
    mvprintw(hstr->promptYNotification, 0, "%s", screenLine);
    if(hstr->theme & HSTR_THEME_COLOR) {
        color_attr_off(A_BOLD);
        color_attr_on(COLOR_PAIR(HSTR_COLOR_NORMAL));
    }
    clrtoeol();
    refresh();
}

// 즐겨찾기 추가시 설명문 변경됨 배경색 기본 초록
void print_cmd_added_favorite_label(const char* cmd)
{
    char screenLine[CMDLINE_LNG];
    snprintf(screenLine, getmaxx(stdscr), "Command '%s' added to favorites (C-/ to show favorites)", cmd);
    if(hstr->theme & HSTR_THEME_COLOR) {
        color_attr_on(COLOR_PAIR(HSTR_COLOR_INFO));
        color_attr_on(A_BOLD);
    }
    //promptYNotification 알림 창 역할
    mvprintw(hstr->promptYNotification, 0, screenLine);
    if(hstr->theme & HSTR_THEME_COLOR) {
        color_attr_off(A_BOLD);
        color_attr_on(COLOR_PAIR(HSTR_COLOR_NORMAL));
    }
    clrtoeol();
    refresh();
}

// hstr 내부에서 사용  hstr 설명문
void print_history_label(void)
{
    if(hstr->hideHistoryHelp)
        return;

    unsigned width=getmaxx(stdscr);      //getmaxx 최대 x좌표값 반환,  stdscr  창 크기 

    char screenLine[CMDLINE_LNG];   // CMDLINE_LNG 2048 
    // snprintf  ( 버퍼,  출력 크기, "내용",)
#ifdef __APPLE__
    snprintf(screenLine, width, "- HISTORY - view:%s (C-w) - match:%s (C-e) - case:%s (C-t) - %d/%d/%d ",
#else
    snprintf(screenLine, width, "- HISTORY - view:%s (C-/) - match:%s (C-e) - case:%s (C-t) - %d/%d/%d ",
#endif
            HSTR_VIEW_LABELS[hstr->view],
            HSTR_MATCH_LABELS[hstr->matching],
            HSTR_CASE_LABELS[hstr->caseSensitive],
            hstr->history->count,
            hstr->history->rawCount,
            hstr->favorites->count);
            //  HSTR_VIEW_LABELS[hstr->view] : ranking, history, favorit등 보여줌,
            //  hstr 구조체  각각 명령어 숫자 count
    width -= strlen(screenLine);
    unsigned i;
    // 설명문 나머지 뒤 라인을 '-' 로 채움
    for(i=0; i<width; i++) {
        strcat(screenLine, "-");
    }
    // 비트 연산  & AND  DARK 이면 배경속성 추정
    if(hstr->theme & HSTR_THEME_COLOR) {
        color_attr_on(A_BOLD);
    }
    color_attr_on(A_REVERSE);
    // move promptYHistory, 0 커서 이동 후 출력
    mvprintw(hstr->promptYHistory, 0, "%s", screenLine);
    color_attr_off(A_REVERSE);
    if(hstr->theme & HSTR_THEME_COLOR) {
        color_attr_off(A_BOLD);
    }
    refresh();
}

//print_pattern 패턴 프롬포트에 출력
void print_pattern(char* pattern, int y, int x)
{
    if(pattern) {
        color_attr_on(A_BOLD);
        mvprintw(y, x, "%s", pattern);
        color_attr_off(A_BOLD);
        clrtoeol();
    }
}

// selection 에 메모리 할당 함수 
// TODO don't realloc if size doesn't change
void hstr_realloc_selection(unsigned size)
{
    if(hstr->selection) {
        if(size) {
            // realloc 동적 메모리 할당 크기를 변경
            hstr->selection
                =realloc(hstr->selection, sizeof(char*) * size);
            // 정규식 관련 변수
            hstr->selectionRegexpMatch
                =realloc(hstr->selectionRegexpMatch, sizeof(regmatch_t) * size);
        } else {
            free(hstr->selection);
            free(hstr->selectionRegexpMatch);
            hstr->selection=NULL;
            hstr->selectionRegexpMatch=NULL;
        }
    } else {
        if(size) {
            hstr->selection = malloc(sizeof(char*) * size);
            hstr->selectionRegexpMatch = malloc(sizeof(regmatch_t) * size);
        }
    }
}

// 정규식 검색으로 추청
unsigned hstr_make_selection(char* prefix, HistoryItems* history, unsigned maxSelectionCount)
{
    hstr_realloc_selection(maxSelectionCount);

    unsigned i, selectionCount=0;
    char **source;
    unsigned count;

    // HISTORY 1, FAVORITES 2, RANKING 0
    // 기본 명령어 추가 HSTR_VIEW_TEST 3 
    // 디렉토리를 5로 변경하고 4에 날짜보기 추가
    switch(hstr->view) {
    case HSTR_VIEW_HISTORY:
        source=history->rawItems;
        count=history->rawCount;
        break;
    case HSTR_VIEW_FAVORITES:
        source=hstr->favorites->items;
        count=hstr->favorites->count;
        break;
    case HSTR_VIEW_TEST:
        source = mycommandtest->items;
        count = mycommandtest->count;
        break;
    case HSTR_VIEW_DATE:
        source = dateitem->items;
        count = dateitem->count;
        break;
    case HSTR_VIEW_DIRECTORY:
        source = diritem->items;
        count = diritem->count;
        break;
    case HSTR_VIEW_RANKING:
    default:
        source=history->items;
        count=history->count;
        break;
    }
    regmatch_t regexpMatch;
    char regexpErrorMessage[CMDLINE_LNG];
    bool regexpCompilationError=false;
    bool keywordsAllMatch;
    char *keywordsSavePtr=NULL;
    char *keywordsToken=NULL;
    char *keywordsParsedLine=NULL;
    char *keywordsPointerToDelete=NULL;
    for(i=0; i<count && selectionCount<maxSelectionCount; i++) {
        if(source[i]) {
            if(!prefix || !strlen(prefix)) {
                add_to_selection(source[i], &selectionCount);
            } else {
                switch(hstr->matching) {
                case HSTR_MATCH_SUBSTRING:
                    switch(hstr->caseSensitive) {
                    case HSTR_CASE_SENSITIVE:
                        if(source[i]==strstr(source[i], prefix)) {
                            add_to_selection(source[i], &selectionCount);
                        }
                        break;
                    case HSTR_CASE_INSENSITIVE:
                        if(source[i]==strcasestr(source[i], prefix)) {
                            add_to_selection(source[i], &selectionCount);
                        }
                        break;
                    }
                    break;
                case HSTR_MATCH_REGEXP:
                    if(hstr_regexp_match(&(hstr->regexp), prefix, source[i], &regexpMatch, regexpErrorMessage, CMDLINE_LNG)) {
                        hstr->selection[selectionCount]=source[i];
                        hstr->selectionRegexpMatch[selectionCount].rm_so=regexpMatch.rm_so;
                        hstr->selectionRegexpMatch[selectionCount].rm_eo=regexpMatch.rm_eo;
                        selectionCount++;
                    } else {
                        if(!regexpCompilationError) {
                            // TODO fix broken messages - getting just escape sequences
                            // print_regexp_error(regexpErrorMessage);
                            regexpCompilationError=true;
                        }
                    }
                    break;
                case HSTR_MATCH_KEYWORDS:
                    keywordsParsedLine = strdup(prefix);
                    keywordsAllMatch = true;
                    keywordsPointerToDelete = keywordsParsedLine;
                    while(true) {
                        keywordsToken = strtok_r(keywordsParsedLine, " ", &keywordsSavePtr);
                        if (keywordsToken == NULL) {
                            break;
                        }
                        keywordsParsedLine = NULL;
                        switch(hstr->caseSensitive) {
                        case HSTR_CASE_SENSITIVE:
                            if(strstr(source[i], keywordsToken) == NULL) {
                                keywordsAllMatch = false;
                            }
                            break;
                        case HSTR_CASE_INSENSITIVE:
                            if(strcasestr(source[i], keywordsToken) == NULL) {
                                keywordsAllMatch = false;
                            }
                            break;
                        }
                    }
                    if(keywordsAllMatch) {
                        add_to_selection(source[i], &selectionCount);
                    }
                    free(keywordsPointerToDelete);
                    break;
                }
            }
        }
    }

    if(prefix && selectionCount<maxSelectionCount) {
        char *substring;
        for(i=0; i<count && selectionCount<maxSelectionCount; i++) {
            switch(hstr->matching) {
            case HSTR_MATCH_SUBSTRING:
                switch(hstr->caseSensitive) {
                case HSTR_CASE_SENSITIVE:
                    substring = strstr(source[i], prefix);
                    if (substring != NULL && substring!=source[i]) {
                        add_to_selection(source[i], &selectionCount);
                    }
                    break;
                case HSTR_CASE_INSENSITIVE:
                    substring = strcasestr(source[i], prefix);
                    if (substring != NULL && substring!=source[i]) {
                        add_to_selection(source[i], &selectionCount);
                    }
                    break;
                }
                break;
            case HSTR_MATCH_REGEXP:
                // all regexps matched previously - user decides whether match ^ or infix
            break;
            case HSTR_MATCH_KEYWORDS:
                // TODO MD consider adding lines that didn't matched all keywords, but some of them
                //         (ordered by number of matched keywords)
            break;
            }
        }
    }

    hstr->selectionSize=selectionCount;
    return selectionCount;
}

void print_selection_row(char* text, int y, int width, char* pattern)
{
    char screenLine[CMDLINE_LNG];
    char buffer[CMDLINE_LNG];
    hstr_strelide(buffer, text, width>2?width-2:0);
    int size = snprintf(screenLine, width, " %s", buffer);
    if(size < 0) screenLine[0]=0;
    mvprintw(y, 0, "%s", screenLine); clrtoeol();

    if(pattern && strlen(pattern)) {
        color_attr_on(A_BOLD);
        if(hstr->theme & HSTR_THEME_COLOR) {
            color_attr_on(COLOR_PAIR(HSTR_COLOR_MATCH));
        }
        char* p=NULL;
        char* pp=NULL;
        char* keywordsSavePtr=NULL;
        char* keywordsToken=NULL;
        char* keywordsParsedLine=NULL;
        char* keywordsPointerToDelete=NULL;

        switch(hstr->matching) {
        case HSTR_MATCH_SUBSTRING:
            switch(hstr->caseSensitive) {
            case HSTR_CASE_INSENSITIVE:
                p=strcasestr(screenLine, pattern);
                if(p) {
                    snprintf(buffer, strlen(pattern)+1, "%s", p);
                    mvprintw(y, p-screenLine, "%s", buffer);
                }
                break;
            case HSTR_CASE_SENSITIVE:
                p=strstr(screenLine, pattern);
                if(p) {
                    mvprintw(y, p-screenLine, "%s", pattern);
                }
                break;
            }
            break;
        case HSTR_MATCH_REGEXP:
            p=strstr(screenLine, pattern);
            if(p) {
                mvprintw(y, p-screenLine, "%s", pattern);
            }
            break;
        case HSTR_MATCH_KEYWORDS:
            keywordsParsedLine = strdup(pattern);
            keywordsPointerToDelete = keywordsParsedLine;
            while (true) {
                keywordsToken = strtok_r(keywordsParsedLine, " ", &keywordsSavePtr);
                keywordsParsedLine = NULL;
                if (keywordsToken == NULL) {
                    break;
                }
                switch(hstr->caseSensitive) {
                case HSTR_CASE_SENSITIVE:
                    p=strstr(screenLine, keywordsToken);
                    if(p) {
                        mvprintw(y, p-screenLine, "%s", keywordsToken);
                    }
                    break;
                case HSTR_CASE_INSENSITIVE:
                    p=strcasestr(screenLine, keywordsToken);
                    if(p) {
                        pp=strdup(p);
                        pp[strlen(keywordsToken)]=0;
                        mvprintw(y, p-screenLine, "%s", pp);
                        free(pp);
                    }
                    break;
                }
            }
            free(keywordsPointerToDelete);

            break;
        }
        if(hstr->theme & HSTR_THEME_COLOR) {
            color_attr_on(COLOR_PAIR(HSTR_COLOR_NORMAL));
        }
        color_attr_off(A_BOLD);
    }
}

void hstr_print_highlighted_selection_row(char* text, int y, int width)
{
    color_attr_on(A_BOLD);
    if(hstr->theme & HSTR_THEME_COLOR) {
        color_attr_on(COLOR_PAIR(HSTR_COLOR_HIROW));
    } else {
        color_attr_on(A_REVERSE);
    }
    char buffer[CMDLINE_LNG];
    hstr_strelide(buffer, text, width>2?width-2:0);
    char screenLine[CMDLINE_LNG];
    snprintf(screenLine, getmaxx(stdscr)+1, "%s%-*.*s ",
            (terminal_has_colors()?" ":">"),
            getmaxx(stdscr)-2, getmaxx(stdscr)-2, buffer);
    mvprintw(y, 0, "%s", screenLine);
    if(hstr->theme & HSTR_THEME_COLOR) {
        color_attr_on(COLOR_PAIR(HSTR_COLOR_NORMAL));
    } else {
        color_attr_off(A_REVERSE);
    }
    color_attr_off(A_BOLD);
}

// hstr_print_selection -> hstr_make_selection -> hstr_realloc_selection
// maxHistoryItems 변수 끝까지 인수로 받아짐
char* hstr_print_selection(unsigned maxHistoryItems, char* pattern)
{
    char* result=NULL;
    unsigned selectionCount=hstr_make_selection(pattern, hstr->history, maxHistoryItems);
    if (selectionCount > 0) {
        result=hstr->selection[0];
    }
    //recalculate_max_history_items 표시 모양 결정 , 목록 크기 반환 hstr->promptItems
    unsigned height=recalculate_max_history_items();
    unsigned width=getmaxx(stdscr);
    unsigned i;
    int y;
    
    // 커서 목록 시작 지점 이동 후 아래 모든 문자 지움
    move(hstr->promptYItemsStart, 0);
    clrtobot();
    bool labelsAreOnBottom = (hstr->promptBottom && !hstr->helpOnOppositeSide) || (!hstr->promptBottom && hstr->helpOnOppositeSide);
    if(labelsAreOnBottom) {
        // TODO: Why is the reprinting here necessary? Please make a comment.
        print_help_label();
        print_history_label();
    }
    if(hstr->promptBottom) {
        // TODO: Why is the reprinting here necessary? Please make a comment.
        // print_pattern
        print_pattern(pattern, hstr->promptY, print_prompt());
        y=hstr->promptYItemsEnd;
    } else {
        y=hstr->promptYItemsStart;
    }

    int start, count;
    char screenLine[CMDLINE_LNG];
    for(i=0; i<height; ++i) {
        if(i<hstr->selectionSize) {
            // TODO make this function
            // 패턴 문자 비었는지 확인
            if(pattern && strlen(pattern)) {
                if(hstr->matching==HSTR_MATCH_REGEXP) {
                    start=hstr->selectionRegexpMatch[i].rm_so;
                    count=hstr->selectionRegexpMatch[i].rm_eo-start;
                    if(count>CMDLINE_LNG) {
                        count=CMDLINE_LNG-1;
                    }
                    strncpy(screenLine,
                            hstr->selection[i]+start,
                            count);
                    screenLine[count]=0;
                } else {
                    strcpy(screenLine, pattern);
                }
                print_selection_row(hstr->selection[i], y, width, screenLine);
            } else {
                print_selection_row(hstr->selection[i], y, width, pattern);
            }
        } else {
            mvprintw(y, 0, " ");
        }

        if(hstr->promptBottom) {
            y--;
        } else {
            y++;
        }
    }
    refresh();

    return result;
}

void highlight_selection(int selectionCursorPosition, int previousSelectionCursorPosition, char* pattern)
{
    if(previousSelectionCursorPosition!=SELECTION_CURSOR_IN_PROMPT) {
        // TODO make this function
        char buffer[CMDLINE_LNG];
        if(pattern && strlen(pattern) && hstr->matching==HSTR_MATCH_REGEXP) {
            int start=hstr->selectionRegexpMatch[previousSelectionCursorPosition].rm_so;
            int end=hstr->selectionRegexpMatch[previousSelectionCursorPosition].rm_eo-start;
            end = MIN(end,getmaxx(stdscr));
            strncpy(buffer,
                    hstr->selection[previousSelectionCursorPosition]+start,
                    end);
            buffer[end]=0;
        } else {
            strcpy(buffer, pattern);
        }

        int text, y;
        if(hstr->promptBottom) {
            text=hstr->promptItems-previousSelectionCursorPosition-1;
            y=hstr->promptYItemsStart+previousSelectionCursorPosition;
        } else {
            text=previousSelectionCursorPosition;
            y=hstr->promptYItemsStart+previousSelectionCursorPosition;
        }
        print_selection_row(
                hstr->selection[text],
                y,
                getmaxx(stdscr),
                buffer);
    }
    if(selectionCursorPosition!=SELECTION_CURSOR_IN_PROMPT) {
        int text, y;
        if(hstr->promptBottom) {
            text=hstr->promptItems-selectionCursorPosition-1;
            y=hstr->promptYItemsStart+selectionCursorPosition;
        } else {
            text=selectionCursorPosition;
            y=hstr->promptYItemsStart+selectionCursorPosition;
        }
        hstr_print_highlighted_selection_row(hstr->selection[text], y, getmaxx(stdscr));
    }
}

int remove_from_history_model(char* almostDead)
{
    if(hstr->view==HSTR_VIEW_FAVORITES) {
        return (int)favorites_remove(hstr->favorites, almostDead);
    } else {
        // raw & ranked history is pruned first as its items point to system history lines
        int systemOccurences=0, rawOccurences=history_mgmt_remove_from_raw(almostDead, hstr->history);
        history_mgmt_remove_from_ranked(almostDead, hstr->history);
        if(rawOccurences) {
            systemOccurences=history_mgmt_remove_from_system_history(almostDead);
        }
        if(systemOccurences!=rawOccurences && hstr->debugLevel>HSTR_DEBUG_LEVEL_NONE) {
            fprintf(stderr, "WARNING: system and raw items deletion mismatch %d / %d\n", systemOccurences, rawOccurences);
        }
        return systemOccurences;
    }
}


void hstr_next_view(void)
{
    hstr->view++;
    // 3의 나머지 로 0,1,2,3 순환
    // 날짜추가로 4->5 변경했음
    hstr->view=hstr->view%5;
}

void stdout_history_and_return(void)
{
    unsigned selectionCount=hstr_make_selection(hstr->cmdline, hstr->history, hstr->history->rawCount);
    if (selectionCount > 0) {
        unsigned i;
        for(i=0; i<selectionCount; i++) {
            printf("%s\n",hstr->selection[i]);
        }
    }
}

// IMPROVE hstr doesn't have to be passed as parameter - it's global static
char* getResultFromSelection(int selectionCursorPosition, Hstr* hstr, char* result) {
    if (hstr->promptBottom) {
        result=hstr->selection[hstr->promptItems-selectionCursorPosition-1];
    } else {
        result=hstr->selection[selectionCursorPosition];
    }
    return result;
}

void hide_notification(void)
{
    if(!hstr->hideBasicHelp) {
        print_help_label();
    } else {
        if(!hstr->hideHistoryHelp) {
            print_history_label();
        } else {
            // TODO: If possible, we should rerender the command list here,
            //  because one of the items was used to print the notification.
        }
    }
}

void loop_to_select(void)
{
    signal(SIGINT, signal_callback_handler_ctrl_c);

    bool isSubshellHint=FALSE;
    char* isSubshellHintText = getenv(HSTR_ENV_VAR_IS_SUBSHELL);
    if(isSubshellHintText && strlen(isSubshellHintText)>0) {
        isSubshellHint=TRUE;
    }

    hstr_curses_start();
    // TODO move the code below to hstr_curses
    color_init_pair(HSTR_COLOR_NORMAL, -1, -1);
    if(hstr->theme & HSTR_THEME_COLOR) {
        color_init_pair(HSTR_COLOR_HIROW, COLOR_WHITE, COLOR_GREEN);
        color_init_pair(HSTR_COLOR_PROMPT, COLOR_BLUE, -1);
        color_init_pair(HSTR_COLOR_DELETE, COLOR_WHITE, COLOR_RED);
        color_init_pair(HSTR_COLOR_MATCH, COLOR_RED, -1);
    }

    color_attr_on(COLOR_PAIR(HSTR_COLOR_NORMAL));
    // TODO why do I print non-filtered selection when on command line there is a pattern?
    hstr_print_selection(recalculate_max_history_items(), NULL);
    color_attr_off(COLOR_PAIR(HSTR_COLOR_NORMAL));
    bool labelsAreOnBottom = (hstr->promptBottom && !hstr->helpOnOppositeSide) || (!hstr->promptBottom && hstr->helpOnOppositeSide);
    if(!labelsAreOnBottom) {
        // TODO: Why is the reprinting here necessary? Please make a comment.
        print_help_label();
        print_history_label();
    }

    bool done=FALSE, skip=TRUE, executeResult=FALSE, lowercase=TRUE;
    bool hideNotificationOnNextTick=TRUE, fixCommand=FALSE, editCommand=FALSE;
    unsigned basex=print_prompt();
    int x=basex, c, cc, cursorX=0, cursorY=0, maxHistoryItems, deletedOccurences;
    int width=getmaxx(stdscr);
    int selectionCursorPosition=SELECTION_CURSOR_IN_PROMPT;
    int previousSelectionCursorPosition=SELECTION_CURSOR_IN_PROMPT;
    char *result="", *msg, *almostDead;
    char pattern[SELECTION_PREFIX_MAX_LNG];
    pattern[0]=0;
    // TODO this is too late! > don't render twice
    // TODO overflow
    strcpy(pattern, hstr->cmdline);

    while (!done) {
        maxHistoryItems=recalculate_max_history_items();

        if(!skip) {
            c = wgetch(stdscr);
        } else {
            if(strlen(pattern)) {
                color_attr_on(A_BOLD);
                mvprintw(hstr->promptY, basex, "%s", pattern);
                color_attr_off(A_BOLD);
                cursorX=getcurx(stdscr);
                cursorY=getcury(stdscr);
                result=hstr_print_selection(maxHistoryItems, pattern);
                move(cursorY, cursorX);
            }
            skip=FALSE;
            continue;
        }

        if(hideNotificationOnNextTick) {
            hide_notification();
            hideNotificationOnNextTick=FALSE;
        }

        if(c == K_CTRL_R) {
            c = (hstr->promptBottom ? K_CTRL_P : K_CTRL_N);
        }

        switch (c) {
        case KEY_HOME:
            // avoids printing of wild chars in search prompt
            break;
        case KEY_END:
            // avoids printing of wild chars in search prompt
            break;
        case KEY_DC: // DEL
            if(selectionCursorPosition!=SELECTION_CURSOR_IN_PROMPT) {
                almostDead=getResultFromSelection(selectionCursorPosition, hstr, result);
                msg=malloc(strlen(almostDead)+1);
                strcpy(msg, almostDead);

                if(!hstr->noConfirm) {
                    print_confirm_delete(msg);
                    cc = wgetch(stdscr);
                }
                if(hstr->noConfirm || cc == 'y') {
                    deletedOccurences=remove_from_history_model(msg);
                    result=hstr_print_selection(maxHistoryItems, pattern);
                    print_cmd_deleted_label(msg, deletedOccurences);
                } else {
                    hide_notification();
                }
                free(msg);
                move(hstr->promptY, basex+strlen(pattern));
                hideNotificationOnNextTick=TRUE;
                print_history_label();  // TODO: Why is this necessary? Add comment!

                if(hstr->selectionSize == 0) {
                    // just update the cursor, there are no elements to select
                    move(hstr->promptY, basex+strlen(pattern));
                    break;
                }

                if(hstr->promptBottom) {
                    if(selectionCursorPosition < (int)(hstr->promptItems-hstr->selectionSize)) {
                        selectionCursorPosition=hstr->promptItems-hstr->selectionSize;
                    }
                } else {
                    if(selectionCursorPosition >= (int)hstr->selectionSize) {
                        selectionCursorPosition = hstr->selectionSize-1;
                    }
                }
                highlight_selection(selectionCursorPosition, SELECTION_CURSOR_IN_PROMPT, pattern);
                move(hstr->promptY, basex+strlen(pattern));
            }
            break;
        case K_CTRL_E:
            hstr->matching++;
            hstr->matching=hstr->matching%HSTR_NUM_HISTORY_MATCH;
            // TODO make this a function
            result=hstr_print_selection(maxHistoryItems, pattern);
            print_history_label();
            selectionCursorPosition=SELECTION_CURSOR_IN_PROMPT;
            if(strlen(pattern)<(width-basex-1)) {
                print_pattern(pattern, hstr->promptY, basex);
                cursorX=getcurx(stdscr);
                cursorY=getcury(stdscr);
            }
            break;
        case K_CTRL_T:
            hstr->caseSensitive=!hstr->caseSensitive;
            hstr->regexp.caseSensitive=hstr->caseSensitive;
            result=hstr_print_selection(maxHistoryItems, pattern);
            print_history_label();
            selectionCursorPosition=SELECTION_CURSOR_IN_PROMPT;
            if(strlen(pattern)<(width-basex-1)) {
                print_pattern(pattern, hstr->promptY, basex);
                cursorX=getcurx(stdscr);
                cursorY=getcury(stdscr);
            }
            break;
#ifdef __APPLE__
        // reserved for view rotation on macOS
        case K_CTRL_W:
#endif
    //컨트롤 + / 슬래쉬 목록 표시 변경
        case K_CTRL_SLASH:
            // hstr->view++;
            hstr_next_view();
            //result 1281줄에 선언 
            result=hstr_print_selection(maxHistoryItems, pattern);
            print_history_label();
            // TODO function
            selectionCursorPosition=SELECTION_CURSOR_IN_PROMPT;
            if(strlen(pattern)<(width-basex-1)) {
                print_pattern(pattern, hstr->promptY, basex);
                cursorX=getcurx(stdscr);
                cursorY=getcury(stdscr);
            }
            break;
        // 하위 디렉토리 탐색 
        case K_CTRL_H:
            // 날짜를 4에 추가하고 디렉토리를 5로 변경
            hstr->view = 5;
            result=hstr_print_selection(maxHistoryItems, pattern);
            print_history_label();
            selectionCursorPosition=SELECTION_CURSOR_IN_PROMPT;
            if(strlen(pattern)<(width-basex-1)) {
                print_pattern(pattern, hstr->promptY, basex);
                cursorX=getcurx(stdscr);
                cursorY=getcury(stdscr);
            }
            break;
        // 태그 추가
        case K_CTRL_B:
            if(selectionCursorPosition!=SELECTION_CURSOR_IN_PROMPT) {
                result=getResultFromSelection(selectionCursorPosition, hstr, result);
                if(hstr->view==HSTR_VIEW_FAVORITES) {
                    favorites_tag_add(hstr->favorites, result);
                } else {
                    favorites_add(hstr->favorites, result);
                    favorites_tag_add(hstr->favorites,result);
                }
                hstr_print_selection(maxHistoryItems, pattern);
                selectionCursorPosition=SELECTION_CURSOR_IN_PROMPT;
                if(hstr->view!=HSTR_VIEW_FAVORITES) {
                    print_cmd_added_favorite_label(result);
                    hideNotificationOnNextTick=TRUE;
                }
                // TODO code review
                if(strlen(pattern)<(width-basex-1)) {
                    print_pattern(pattern, hstr->promptY, basex);
                    cursorX=getcurx(stdscr);
                    cursorY=getcury(stdscr);
                }
            }
            break;
        case K_CTRL_F:
            if(selectionCursorPosition!=SELECTION_CURSOR_IN_PROMPT) {
                result=getResultFromSelection(selectionCursorPosition, hstr, result);
                if(hstr->view==HSTR_VIEW_FAVORITES) {
                    favorites_choose(hstr->favorites, result);
                } else {
                    favorites_add(hstr->favorites, result);
                }
                hstr_print_selection(maxHistoryItems, pattern);
                selectionCursorPosition=SELECTION_CURSOR_IN_PROMPT;
                if(hstr->view!=HSTR_VIEW_FAVORITES) {
                    print_cmd_added_favorite_label(result);
                    hideNotificationOnNextTick=TRUE;
                }
                // TODO code review
                if(strlen(pattern)<(width-basex-1)) {
                    print_pattern(pattern, hstr->promptY, basex);
                    cursorX=getcurx(stdscr);
                    cursorY=getcury(stdscr);
                }
            }
            break;
        case KEY_RESIZE:
            print_history_label();
            maxHistoryItems=recalculate_max_history_items();
            result=hstr_print_selection(maxHistoryItems, pattern);
            print_history_label();
            selectionCursorPosition=SELECTION_CURSOR_IN_PROMPT;
            move(hstr->promptY, basex+strlen(pattern));
            break;
#ifndef __APPLE__
        case K_CTRL_W: // TODO supposed to delete just one word backward
#endif
        case K_CTRL_U:
            pattern[0]=0;
            print_pattern(pattern, hstr->promptY, basex);
            break;
        case K_CTRL_L:
            toggle_case(pattern, lowercase);
            lowercase=!lowercase;
            print_pattern(pattern, hstr->promptY, basex);
            selectionCursorPosition=SELECTION_CURSOR_IN_PROMPT;
            break;
        case K_BACKSPACE:
        case KEY_BACKSPACE:
            if(hstr_strlen(pattern)>0) {
                hstr_chop(pattern);
                x--;
                print_pattern(pattern, hstr->promptY, basex);
            }

            // TODO why I make selection if it's done in print_selection?
            if(strlen(pattern)>0) {
                hstr_make_selection(pattern, hstr->history, maxHistoryItems);
            } else {
                hstr_make_selection(NULL, hstr->history, maxHistoryItems);
            }
            result=hstr_print_selection(maxHistoryItems, pattern);

            move(hstr->promptY, basex+hstr_strlen(pattern));
            break;
        case KEY_UP:
        case K_CTRL_K:
        case K_CTRL_P:
            previousSelectionCursorPosition=selectionCursorPosition;
            if(selectionCursorPosition>0) {
                if(hstr->promptBottom) {
                    if(selectionCursorPosition <= (int)(hstr->promptItems-hstr->selectionSize)) {
                        selectionCursorPosition=hstr->promptItems-1;
                    } else {
                        selectionCursorPosition--;
                    }
                } else {
                    selectionCursorPosition--;
                }
            } else {
                if(hstr->promptBottom) {
                    selectionCursorPosition=hstr->promptItems-1;
                } else {
                    selectionCursorPosition=hstr->selectionSize-1;
                }
            }
            highlight_selection(selectionCursorPosition, previousSelectionCursorPosition, pattern);
            move(hstr->promptY, basex+strlen(pattern));
            break;
        case KEY_PPAGE:
            previousSelectionCursorPosition=selectionCursorPosition;
            if(selectionCursorPosition>=PG_JUMP_SIZE) {
                selectionCursorPosition=selectionCursorPosition-PG_JUMP_SIZE;
            } else {
                selectionCursorPosition=0;
            }
            highlight_selection(selectionCursorPosition, previousSelectionCursorPosition, pattern);
            move(hstr->promptY, basex+strlen(pattern));
            break;
        case KEY_DOWN:
        case K_CTRL_J:
        case K_CTRL_N:
            if(selectionCursorPosition==SELECTION_CURSOR_IN_PROMPT) {
                if(hstr->promptBottom) {
                    selectionCursorPosition=hstr->promptItems-hstr->selectionSize;
                } else {
                    selectionCursorPosition=previousSelectionCursorPosition=0;
                }
            } else {
                previousSelectionCursorPosition=selectionCursorPosition;
                if(hstr->promptBottom) {
                    if(selectionCursorPosition<hstr->promptItems-1) {
                        selectionCursorPosition++;
                    } else {
                        selectionCursorPosition=hstr->promptItems-hstr->selectionSize;
                    }
                } else {
                    if((selectionCursorPosition+1) < (int)hstr->selectionSize) {
                        selectionCursorPosition++;
                    } else {
                        selectionCursorPosition=0;
                    }
                }
            }
            if(hstr->selectionSize) {
                highlight_selection(selectionCursorPosition, previousSelectionCursorPosition, pattern);
            }
            move(hstr->promptY, basex+strlen(pattern));
            break;
        case KEY_NPAGE:
            if(selectionCursorPosition==SELECTION_CURSOR_IN_PROMPT) {
                selectionCursorPosition=previousSelectionCursorPosition=0;
            } else {
                previousSelectionCursorPosition=selectionCursorPosition;
                if((selectionCursorPosition+PG_JUMP_SIZE) < (int)hstr->selectionSize) {
                    selectionCursorPosition = selectionCursorPosition+PG_JUMP_SIZE;
                } else {
                    selectionCursorPosition=hstr->selectionSize-1;
                }
            }
            if(hstr->selectionSize) {
                highlight_selection(selectionCursorPosition, previousSelectionCursorPosition, pattern);
            }
            move(hstr->promptY, basex+strlen(pattern));
            break;
        case K_ENTER:
        case KEY_ENTER:
            executeResult=TRUE;
            if(selectionCursorPosition!=SELECTION_CURSOR_IN_PROMPT) {
                result=getResultFromSelection(selectionCursorPosition, hstr, result);
                if(hstr->view==HSTR_VIEW_FAVORITES) {
                    favorites_choose(hstr->favorites,result);
                }
            }
            else {
                if (hstr->selectionSize > 0) {
                    result=hstr->selection[0];
                }
            }
            done=TRUE;
            break;
        case KEY_LEFT:
            fixCommand=TRUE;
            executeResult=TRUE;
            if(selectionCursorPosition!=SELECTION_CURSOR_IN_PROMPT) {
                result=getResultFromSelection(selectionCursorPosition, hstr, result);
                if(hstr->view==HSTR_VIEW_FAVORITES) {
                    favorites_choose(hstr->favorites,result);
                }
            } else {
                result=pattern;
            }
            done=TRUE;
            break;
        case K_TAB:
        case KEY_RIGHT:
            if(!isSubshellHint) {
                editCommand=TRUE;
            } else {
                // Not setting editCommand to TRUE here,
                // because else an unnecessary blank line gets emitted before returning to prompt.
            }
            if(selectionCursorPosition!=SELECTION_CURSOR_IN_PROMPT) {
                result=getResultFromSelection(selectionCursorPosition, hstr, result);
                if(hstr->view==HSTR_VIEW_FAVORITES) {
                    favorites_choose(hstr->favorites,result);
                }
            } else {
                result=pattern;
            }
            done=TRUE;
            break;
        case K_CTRL_G:
        case K_ESC:
            result=NULL;
            history_mgmt_clear_dirty();
            done=TRUE;
            break;
        case K_CTRL_X:
            result=NULL;
            done=TRUE;
            break;
        default:
            LOGKEYS(Y_OFFSET_HELP, c);
            LOGCURSOR(Y_OFFSET_HELP);
            LOGUTF8(Y_OFFSET_HELP,pattern);
            LOGSELECTION(Y_OFFSET_HELP,getmaxy(stdscr),hstr->selectionSize);

            if(c>K_CTRL_Z) {
                selectionCursorPosition=SELECTION_CURSOR_IN_PROMPT;

                if(strlen(pattern)<(width-basex-1)) {
                    strcat(pattern, (char*)(&c));
                    print_pattern(pattern, hstr->promptY, basex);
                    cursorX=getcurx(stdscr);
                    cursorY=getcury(stdscr);
                }

                result = hstr_print_selection(maxHistoryItems, pattern);
                move(cursorY, cursorX);
                refresh();
            }
            break;
        }
    }
    hstr_curses_stop(hstr->keepPage);

    if(result!=NULL) {
        if(fixCommand) {
            fill_terminal_input("fc \"", FALSE);
        }
        fill_terminal_input(result, editCommand);
        if(fixCommand) {
            fill_terminal_input("\"", FALSE);
        }
        if(executeResult) {
            fill_terminal_input("\n", FALSE);
        }
    }
}

// TODO protection from line overflow (snprinf)
void hstr_assemble_cmdline_pattern(int argc, char* argv[], int startIndex)
{
    if(argc>0) {
        int i;
        for(i=startIndex; i<argc; i++) {
            if((strlen(hstr->cmdline)+strlen(argv[i])*2)>CMDLINE_LNG) break;
            if(strstr(argv[i], " ")) {
                strcat(hstr->cmdline, "\"");
            }
            strcat(hstr->cmdline, argv[i]);
            if(strstr(argv[i], " ")) {
                strcat(hstr->cmdline, "\"");
            }
            if((i+1<argc)) {
                strcat(hstr->cmdline, " ");
            }
        }
    }
}

void hstr_interactive(void)
{
    hstr->history=prioritized_history_create(hstr->bigKeys, hstr->blacklist.set);
    if(hstr->history) {
        history_mgmt_open();
        if(hstr->interactive) {
            loop_to_select();
        } else {
            stdout_history_and_return();
        }
        history_mgmt_flush();
    } // else (no history) handled in create() method

    hstr_exit(EXIT_SUCCESS);
}

void hstr_getopt(int argc, char **argv)
{
    int option_index = 0;
    int option = getopt_long(argc, argv, "fkVhnszb", long_options, &option_index);
    if(option != -1) {
        switch(option) {
        case 'f':
            hstr->view=HSTR_VIEW_FAVORITES;
            break;
        case 'n':
            hstr->interactive=false;
            break;
        case 'k':
            if(history_mgmt_remove_last_history_entry(hstr->verboseKill)) {
                hstr_exit(EXIT_SUCCESS);
                break;
            } else {
                hstr_exit(EXIT_FAILURE);
                break;
            }
        case 'b':
            blacklist_load(&hstr->blacklist);
            blacklist_dump(&hstr->blacklist);
            hstr_exit(EXIT_SUCCESS);
            break;
        case 'V':
            printf("%s", VERSION_STRING);
            hstr_exit(EXIT_SUCCESS);
            break;
        case 'h':
            printf("%s", HELP_STRING);
            hstr_exit(EXIT_SUCCESS);
            break;
        case 'z':
            printf("%s", INSTALL_ZSH_STRING);
            hstr_exit(EXIT_SUCCESS);
            break;
        case 's':
            // ZSH_VERSION is not exported by zsh > detected by parent process name
            if(isZshParentShell()) {
                printf("%s", INSTALL_ZSH_STRING);
            } else {
                printf("%s", INSTALL_BASH_STRING);
            }
            hstr_exit(EXIT_SUCCESS);
            break;
        case '?':
        default:
            printf("%s", HELP_STRING);
            hstr_exit(EXIT_SUCCESS);
        }
    }

    if(optind < argc) {
        hstr_assemble_cmdline_pattern(argc, argv, optind);
    }
}

int hstr_main(int argc, char* argv[])
{
    setlocale(LC_ALL, "");

    // 기본 명령어 추가 구조체 mycommandtest 에 메모리 할당
    mycommandtest = malloc(sizeof(MyCommandItem));
    // 하위 디렉토리 구조체 메모리 할당
    diritem = malloc(sizeof(DirItem));
    // 날짜구조체
    dateitem = malloc(sizeof(DateItem));
    hstr=malloc(sizeof(Hstr));
    hstr_init();

    hstr_get_env_configuration();
    hstr_getopt(argc, argv);
    favorites_get(hstr->favorites);
    blacklist_load(&hstr->blacklist);
    //  기본 명령어 추가  저장된 파일 불러오기
    MyCommandItem_get(mycommandtest);
    //하위 디렉토리 탐색
    DirItem_get();
    // 날짜 불러오기
    DateItem_get(dateitem);
    // hstr cleanup is handled by hstr_exit()
    hstr_interactive();

    return EXIT_SUCCESS;
}