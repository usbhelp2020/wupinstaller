#include <string.h>
#include <malloc.h>
#include "dynamic_libs/os_functions.h"
#include "dynamic_libs/fs_functions.h"
#include "dynamic_libs/sys_functions.h"
#include "dynamic_libs/vpad_functions.h"
#include "dynamic_libs/socket_functions.h"
#include "system/memory.h"
#include "utils/logger.h"
#include "utils/utils.h"
#include "common/common.h"
#include "fs/sd_fat_devoptab.h"
#include <dirent.h>

#define TITLE_TEXT                  "WUP installer by crediar (HBL version 1.0 by Dimok)"
#define TITLE_TEXT2                 "[Mod 1.1 by Yardape8000]"

#define MCP_COMMAND_INSTALL_ASYNC   0x81
#define MAX_INSTALL_PATH_LENGTH     0x27F

static int installCompleted = 0;
static int installSuccess = 0;
static int installToUsb = 0;
static u32 installError = 0;
static u64 installedTitle = 0;
static int dirNum = 0;
static char installFolder[256] = "";
static bool folderSelect[1024] = {false};

static void PrintError2(const char *errorStr, const char *errorStr2)
{
    for(int i = 0; i < 2; i++)
    {
        OSScreenClearBufferEx(i, 0);
        OSScreenPutFontEx(i, 0, 0, TITLE_TEXT);
        OSScreenPutFontEx(i, 0, 2, errorStr);
        OSScreenPutFontEx(i, 0, 3, errorStr2);
        OSScreenFlipBuffersEx(i);
    }
    sleep(4);
}

static void PrintError(const char *errorStr)
{
	PrintError2(errorStr, "");
}

static int IosInstallCallback(unsigned int errorCode, unsigned int * priv_data)
{
    installError = errorCode;
    installCompleted = 1;
    return 0;
}

static void InstallTitle(const char *titlePath)
{
    //!---------------------------------------------------
    //! This part of code originates from Crediars MCP patcher assembly code
    //! it is just translated to C
    //!---------------------------------------------------
    unsigned int mcpHandle = MCP_Open();
    if(mcpHandle == 0)
    {
        PrintError("Failed to open MCP.");
        return;
    }

    char text[256];
    unsigned int * mcpInstallInfo = (unsigned int *)OSAllocFromSystem(0x24, 0x40);
    char * mcpInstallPath = (char *)OSAllocFromSystem(MAX_INSTALL_PATH_LENGTH, 0x40);
    unsigned int * mcpPathInfoVector = (unsigned int *)OSAllocFromSystem(0x0C, 0x40);

    do
    {
        if(!mcpInstallInfo || !mcpInstallPath || !mcpPathInfoVector)
        {
            PrintError("Error: Could not allocate memory.");
            break;
        }

        __os_snprintf(text, sizeof(text), titlePath);

        int result = MCP_InstallGetInfo(mcpHandle, text, mcpInstallInfo);
        if(result != 0)
        {
            __os_snprintf(text, sizeof(text), "Error: MCP_InstallGetInfo 0x%08X", MCP_GetLastRawError());
            PrintError2(text, "Confirm encrypted WUP files are in the proper directory/folder");
            break;
        }

        u32 titleIdHigh = mcpInstallInfo[0];
        u32 titleIdLow = mcpInstallInfo[1];
		int installableToUsb = 0;

        if(   (titleIdHigh == 0x0005000E)     // game update
           || (titleIdHigh == 0x00050000)     // game
           || (titleIdHigh == 0x0005000C)     // DLC
           || (titleIdHigh == 0x00050002))    // Demo
		   installableToUsb = 1;
        if( installableToUsb
		   || (titleIdLow == 0x10041000)     // JAP Version.bin
           || (titleIdLow == 0x10041100)     // USA Version.bin
           || (titleIdLow == 0x10041200))    // EUR Version.bin
        {
            installedTitle = ((u64)titleIdHigh << 32ULL) | titleIdLow;

            if(installToUsb && installableToUsb)
            {
                result = MCP_InstallSetTargetDevice(mcpHandle, 1);
                if(result != 0)
                {
                    __os_snprintf(text, sizeof(text), "Error: MCP_InstallSetTargetDevice 0x%08X", MCP_GetLastRawError());
                    PrintError(text);
                    break;
                }
                result = MCP_InstallSetTargetUsb(mcpHandle, 1);
                if(result != 0)
                {
                    __os_snprintf(text, sizeof(text), "Error: MCP_InstallSetTargetUsb 0x%08X", MCP_GetLastRawError());
                    PrintError(text);
                    break;
                }
            }

            mcpInstallInfo[2] = (unsigned int)MCP_COMMAND_INSTALL_ASYNC;
            mcpInstallInfo[3] = (unsigned int)mcpPathInfoVector;
            mcpInstallInfo[4] = (unsigned int)1;
            mcpInstallInfo[5] = (unsigned int)0;

            memset(mcpInstallPath, 0, MAX_INSTALL_PATH_LENGTH);
            __os_snprintf(mcpInstallPath, MAX_INSTALL_PATH_LENGTH, titlePath);
            memset(mcpPathInfoVector, 0, 0x0C);

            mcpPathInfoVector[0] = (unsigned int)mcpInstallPath;
            mcpPathInfoVector[1] = (unsigned int)MAX_INSTALL_PATH_LENGTH;

            result = IOS_IoctlvAsync(mcpHandle, MCP_COMMAND_INSTALL_ASYNC, 1, 0, mcpPathInfoVector, IosInstallCallback, mcpInstallInfo);
            if(result != 0)
            {
                __os_snprintf(text, sizeof(text), "Error: MCP_InstallTitleAsync 0x%08X", MCP_GetLastRawError());
                PrintError(text);
                break;
            }

            while(!installCompleted)
            {
                memset(mcpInstallInfo, 0, 0x24);

                result = MCP_InstallGetProgress(mcpHandle, mcpInstallInfo);

                if(mcpInstallInfo[0] == 1)
                {
                    u64 installedSize, totalSize;
					totalSize = ((u64)mcpInstallInfo[3] << 32ULL) | mcpInstallInfo[4];
					installedSize = ((u64)mcpInstallInfo[5] << 32ULL) | mcpInstallInfo[6];
					int percent = (totalSize != 0) ? ((installedSize * 100.0f) / totalSize) : 0;
                    for(int i = 0; i < 2; i++)
                    {
                        OSScreenClearBufferEx(i, 0);

                        OSScreenPutFontEx(i, 0, 0, TITLE_TEXT);
                        OSScreenPutFontEx(i, 0, 1, TITLE_TEXT2);
                        OSScreenPutFontEx(i, 0, 3, "Installing title...");

                        __os_snprintf(text, sizeof(text), "%08X%08X - %0.1f / %0.1f MB (%i%%)", titleIdHigh, titleIdLow, installedSize / (1024.0f * 1024.0f),
                                                                                                  totalSize / (1024.0f * 1024.0f), percent);
                        OSScreenPutFontEx(i, 0, 4, text);

                        if(percent == 100)
                        {
                            OSScreenPutFontEx(i, 0, 5, "Please wait...");
                        }
                        // Flip buffers
                        OSScreenFlipBuffersEx(i);
                    }
                }

                usleep(50000);
            }

            if(installError != 0)
            {
				char text2[256] = "";
                if ((installError == 0xFFFCFFE9) && installToUsb)
				{
                    __os_snprintf(text, sizeof(text), "Error: 0x%08X access failed (no USB storage attached?)", installError);
                }
                else
				{
                    __os_snprintf(text, sizeof(text), "Error: install error code 0x%08X", installError);
					if (installError == 0xFFFBF446)
						__os_snprintf(text2, sizeof(text2), "%s", "Possible missing or bad title.tik file");
					if (installError == 0xFFFBF441)
						__os_snprintf(text2, sizeof(text2), "%s", "Possible incorrect console for DLC title.tik file");
                }
                PrintError2(text, text2);
                break;
            }
            else
            {
                for(int i = 0; i < 2; i++)
                {
                    OSScreenClearBufferEx(i, 0);

                    OSScreenPutFontEx(i, 0, 0, TITLE_TEXT);
                    __os_snprintf(text, sizeof(text), "Installed title %08X-%08X successfully.", mcpInstallInfo[1], mcpInstallInfo[2]);
                    OSScreenPutFontEx(i, 0, 2, text);
                    // Flip buffers
                    OSScreenFlipBuffersEx(i);
                }
                installSuccess = 1;
            }
        }
        else
        {
            __os_snprintf(text, sizeof(text), "Error: Not a version title, game, game update, DLC or demo");
            PrintError(text);
        }

    }
    while(0);

    MCP_Close(mcpHandle);

    if(mcpPathInfoVector)
        OSFreeToSystem(mcpPathInfoVector);
    if(mcpInstallPath)
        OSFreeToSystem(mcpInstallPath);
    if(mcpInstallInfo)
        OSFreeToSystem(mcpInstallInfo);
}

static void GetInstallDir(char *dest, int size)
{
	DIR *dirPtr;
	struct dirent *dirEntry;     
	dirPtr = opendir ("sd:/install/");
	int dirsFound = 0;
	
	if (dirPtr != NULL)
	{
		dirEntry = readdir(dirPtr);
		if (dirEntry != NULL && dirEntry->d_type == DT_DIR)
		{
			seekdir(dirPtr, dirNum);
			dirEntry = readdir(dirPtr);
			closedir (dirPtr);
			if (dirEntry != NULL)
				__os_snprintf(dest, size, "install/%s", dirEntry->d_name);
			else
			{
				dirNum--;
				if (dirNum < 0)
					dirNum = 0;
			}
			dirsFound = 1;
		}
	}
	if (!dirsFound)
	{
		__os_snprintf(dest, size, "install");
		dirNum = 0;
	}
}

static void setAllFolderSelect(bool state)
{
	DIR *dirPtr;
	struct dirent *dirEntry;     
	
	for (int i = 0; i < 1023; i++)
		folderSelect[i] = false;

	if (state)
	{
		dirPtr = opendir ("sd:/install/");
		if (dirPtr != NULL)
		{
			int i = 0;
			while (1)
			{
				dirEntry = readdir(dirPtr);
				if (dirEntry == NULL)
					break;
				folderSelect[i++] = true;
			}
			closedir (dirPtr);
		}
	}
}

static int getNextSelectedFolder(void)
{
	int i;
	for (i = 0; i < 1023; i++)
		if (folderSelect[i] == true)
			break;
	return i;
}

static int useFolderSelect(void)
{
	int i;
	int ret = 0;
	for (i = 0; i < 1023; i++)
		if (folderSelect[i] == true)
		{
			ret = 1;
			break;
		}
	return ret;
}

/* Entry point */
int Menu_Main(void)
{
    //!*******************************************************************
    //!                   Initialize function pointers                   *
    //!*******************************************************************
    //! do OS (for acquire) and sockets first so we got logging
    InitOSFunctionPointers();
    InitFSFunctionPointers();
    InitSysFunctionPointers();
    InitVPadFunctionPointers();
    //InitSocketFunctionPointers();

    //log_init("192.168.178.3");

    //!*******************************************************************
    //!                    Initialize heap memory                        *
    //!*******************************************************************
    //! We don't need bucket and MEM1 memory so no need to initialize
    memoryInitialize();
    mount_sd_fat("sd");

    VPADInit();
	
    int update_screen = 1;
	int delay = 0;
    int doInstall = 0;
    int vpadError = -1;
    VPADData vpad_data;

    // Prepare screen
    int screen_buf0_size = 0;
    int screen_buf1_size = 0;

    // Init screen and screen buffers
    OSScreenInit();
    screen_buf0_size = OSScreenGetBufferSizeEx(0);
    screen_buf1_size = OSScreenGetBufferSizeEx(1);

    unsigned char *screenBuffer = MEM1_alloc(screen_buf0_size + screen_buf1_size, 0x40);

    OSScreenSetBufferEx(0, screenBuffer);
    OSScreenSetBufferEx(1, (screenBuffer + screen_buf0_size));

    OSScreenEnableEx(0, 1);
    OSScreenEnableEx(1, 1);

    // in case we are not in mii maker but in system menu we start the installation
    if (OSGetTitleID() != 0x000500101004A200 && // mii maker eur
        OSGetTitleID() != 0x000500101004A100 && // mii maker usa
        OSGetTitleID() != 0x000500101004A000)   // mii maker jpn
    {
		char folder[256];
		__os_snprintf(folder, sizeof(folder), "/vol/app_sd/%s", installFolder);
		InstallTitle(folder);

        MEM1_free(screenBuffer);
        memoryRelease();
        SYSLaunchMiiStudio(0);

        return EXIT_RELAUNCH_ON_LOAD;
    }

	folderSelect[dirNum] = false;
	if (installSuccess && useFolderSelect())
	{
		dirNum = getNextSelectedFolder();
		doInstall = 1;
		delay = 250;
	}
	
    while(1)
    {
		// print to TV and DRC
		if(update_screen)
		{
			GetInstallDir(installFolder, sizeof(installFolder));
			OSScreenClearBufferEx(0, 0);
			OSScreenClearBufferEx(1, 0);
			for(int i = 0; i < 2; i++)
			{
				char text[80];

				OSScreenPutFontEx(i, 0, 0, TITLE_TEXT);
				OSScreenPutFontEx(i, 0, 1, TITLE_TEXT2);
				if(installSuccess)
				{
					__os_snprintf(text, sizeof(text), "Install of title %08X-%08X finished successfully.", (u32)(installedTitle >> 32), (u32)(installedTitle & 0xffffffff));

					OSScreenPutFontEx(i, 0, 3, text);
					if (!doInstall)
						OSScreenPutFontEx(i, 0, 4, "You can eject the SD card now (if wanted).");
				}

				if (!doInstall)
				{
					OSScreenPutFontEx(i, 0, 6, "Select Install Folder:");
					__os_snprintf(text, sizeof(text), "%c  %s", folderSelect[dirNum] ? '*' : ' ', installFolder);
					OSScreenPutFontEx(i, 0, 7, text);

					OSScreenPutFontEx(i, 0, 9, "Press A-Button to install title(s) to system memory.");
					OSScreenPutFontEx(i, 0, 10, "Press X-Button to install title(s) to USB storage.");
				}
				else
				{
					OSScreenPutFontEx(i, 0, 5, installFolder);
					__os_snprintf(text, sizeof(text), "Will install in %d", delay / 50);
					OSScreenPutFontEx(i, 0, 6, text);
					OSScreenPutFontEx(i, 0, 7, "Press B-Button to Cancel");
				}

				OSScreenPutFontEx(i, 0, 11, "Press HOME-Button to return to HBL.");
			}

				// Flip buffers
				OSScreenFlipBuffersEx(0);
				OSScreenFlipBuffersEx(1);
		}
		update_screen = 0;

        VPADRead(0, &vpad_data, 1, &vpadError);
		u32 pressedBtns = 0;
		if (!vpadError)
			pressedBtns = vpad_data.btns_d | vpad_data.btns_h;
		
		if (pressedBtns & VPAD_BUTTON_HOME)
			break;

        if (!doInstall)
        {
			if (pressedBtns & VPAD_BUTTON_A)
			{
				doInstall = 1;
				installToUsb = 0;
				if (useFolderSelect())
				{
					dirNum = 0;
					dirNum = getNextSelectedFolder();
				}
				break;
			}

			if (pressedBtns & VPAD_BUTTON_X)
			{
				doInstall = 1;
				installToUsb = 1;
				if (useFolderSelect())
				{
					dirNum = 0;
					dirNum = getNextSelectedFolder();
				}
				break;
			}

			// Up/Down Buttons
			if (pressedBtns & VPAD_BUTTON_UP)
			{
				if (--delay <= 0)
				{
					if (dirNum < 1000)
						dirNum++;
					else
						dirNum = 0;
					delay = (vpad_data.btns_d & VPAD_BUTTON_UP) ? 6 : 0;
				}
			}
			else if (pressedBtns & VPAD_BUTTON_DOWN)
			{
				if (--delay <= 0)
				{
					if (dirNum > 0)
						dirNum--;
					delay = (vpad_data.btns_d & VPAD_BUTTON_DOWN) ? 6 : 0;
				}
			}
			else
			{
				delay = 0;
			}
			
			if (pressedBtns & (VPAD_BUTTON_LEFT | VPAD_BUTTON_RIGHT))
			{
				folderSelect[dirNum] = (pressedBtns & VPAD_BUTTON_RIGHT) ? 1 : 0;
			}

			if (pressedBtns & (VPAD_BUTTON_PLUS | VPAD_BUTTON_MINUS))
			{
				setAllFolderSelect((pressedBtns & VPAD_BUTTON_PLUS) ? true : false);
			}

			// folder selection button pressed ?
			update_screen = (pressedBtns & (VPAD_BUTTON_UP | VPAD_BUTTON_DOWN | VPAD_BUTTON_LEFT | VPAD_BUTTON_RIGHT | VPAD_BUTTON_PLUS | VPAD_BUTTON_MINUS)) ? 1 : 0;
		}
		else
		{
			if (pressedBtns & VPAD_BUTTON_B)
			{
				doInstall = 0;
				installSuccess = 0;
				update_screen = 1;
				delay = 0;
			}
			else if (--delay <= 0)
				break;
			else
				update_screen = (delay % 50 == 0) ? 1 : 0;
		}

		usleep(20000);
    }

	MEM1_free(screenBuffer);
	screenBuffer = NULL;

    //!*******************************************************************
    //!                    Enter main application                        *
    //!*******************************************************************
    unmount_sd_fat("sd");
    memoryRelease();

    if(doInstall)
    {
        installSuccess = 0;
        installedTitle = 0;
        installCompleted = 0;
        installError = 0;

        SYSLaunchMenu();
        return EXIT_RELAUNCH_ON_LOAD;
    }
	
	setAllFolderSelect(false);
	dirNum = 0;
	installFolder[0] = 0;

    return EXIT_SUCCESS;
}

