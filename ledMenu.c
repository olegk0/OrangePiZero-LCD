#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <signal.h> // Ctrl+c handler
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>
#include <ifaddrs.h> // For network tools
#include <netinet/in.h>
#include <arpa/inet.h>
#include <linux/kernel.h>       /* for struct sysinfo */
#include <sys/sysinfo.h>
//#include <sys/statvfs.h>

#include <wiringPi.h>
#include "ledMenu.h"
#include "pcd8544.h"

#define BTN_PIN 11

// TODO: Use PWM pin (PA06)
//#define LCD_BACKLIGHT 0
// LCD_PINS: CLK, DIN, DC, CS, RST, Contrast (Max: 127)
//#define LCD_PINS 6, 5, 4, 16, 15
#define LCD_PINS 14, 12, 4, 10, 5
#define LCD_CONTRAST 60

void DoNothing()
{
    // Do nothig (Dummy for buttons)
}

void PrintMainScreen1();

static MenuItem MainMenu[] = {
	{ "Show time", ShowTime }, {
		"Power control", PrintPowerSettings },
	{ "Show logo", ShowLogo }, {
		"[MainScreen]", PrintMainScreen1 }, };

static MenuItem PowerSettingsMenu[] = {
	{ "Shutdown", DoShutdown }, {
		"Reboot", DoReboot },
	{ "< Back", PrintMainMenu }, };

// Buttons default state
Button Buttons[2] = {
	{ DoNothing }, };

int ActiveMenuItem = 1; // SHould start from 1 for future tests
int ActiveMenuItems = 0;
static int keepRunning = 1;
MenuItem (*CurrentMenu)[];
void (*DoSomething)();
unsigned long PreviousMllis = 0;

void intHandler(int dummy)
{
    printf("\nGot signal: %d, quit...\n", dummy);
    keepRunning = 0;
}

int getDisksInfo(disk_info *di, unsigned cnts)
{
    FILE *fp;
    char line[1035];
    char sz_used[20];

    fp = popen("df -h", "r");
    if (fp == NULL) {
	printf("Failed to run command\n");
	return -1;
    }

    unsigned dip = 0;
    while (fgets(line, sizeof(line) - 1, fp) != NULL) {
	disk_info *cdi = &di[dip];
	sscanf(line, "%s %s %s %s %s %s", cdi->name, cdi->size, sz_used, cdi->free, cdi->usedPRC, cdi->mount);
	if (strstr(cdi->name, "/dev/sd") == NULL) {
	    continue;
	}
//	printf("%s\n", cdi->name);
	dip++;
	if (dip == cnts) {
//	    printf("in struct full!\n" );
	    break;
	}
    }
    pclose(fp);
    return dip;
}

unsigned getCPUTemp()
{
    FILE *fp;
    int ret = 0;
    if ((fp = fopen("/etc/armbianmonitor/datasources/soctemp", "r")) == NULL) {
	printf("Cannot open directory file.");
    } else {
	fscanf(fp, "%d", &ret);
	fclose(fp);
    }
    return ret / 1000;
}

void getSysInfo(mysysinfo *si)
{
    struct sysinfo s_info;
    int error = sysinfo(&s_info);
    if (error != 0) {
	printf("code error = %d\n", error);
    }
    si->uptime = s_info.uptime;
    si->loads[0] = s_info.loads[0];
    si->loads[1] = s_info.loads[1];
    si->loads[2] = s_info.loads[2];

    si->totalram = s_info.totalram;
    si->freeram = s_info.freeram;
}

void DrawMenu()
{
    printf("DrawMenu(); Item: %d of %d\n", ActiveMenuItem, ActiveMenuItems);
    LCDclear();
    // TODO: Scroll
    for (int i = 0; i < ActiveMenuItems; i++) {
	/*
	 if (i == (ActiveMenuItem - 1)) {
	 LCDfillrect(0, i * 8, LCDWIDTH, 9, BLACK);
	 LCDSetFontColor(WHITE);
	 LCDdrawstring(0, i * 8 + 1, ">");
	 LCDdrawstring(6, i * 8 + 1, (*CurrentMenu)[i].Name);
	 LCDSetFontColor(BLACK);
	 } else {
	 LCDdrawstring(6, i * 8 + 1, (*CurrentMenu)[i].Name);
	 }
	 */
	LCDdrawstring(0, i * 8 + 1, (i == (ActiveMenuItem - 1)) ? ">" : " ");
	LCDdrawstring(6, i * 8 + 1, (*CurrentMenu)[i].Name);
    }
    LCDdisplay();
}

void RunSelected()
{
    printf("RunSelected(); Item #%d\n", ActiveMenuItem);
    (*CurrentMenu)[ActiveMenuItem - 1].Run();
}

void MenuUp()
{
    printf("MenuUp()\n");
    if ((ActiveMenuItem - 1) < 1)
	ActiveMenuItem = ActiveMenuItems;
    else
	ActiveMenuItem--;
    DrawMenu();
}

void MenuDown()
{
    printf("MenuDown()\n");
    if ((ActiveMenuItem + 1) > ActiveMenuItems)
	ActiveMenuItem = 1;
    else
	ActiveMenuItem++;
    DrawMenu();
}

void drawMainScreen1()
{
    unsigned long CurrentMillis = millis();
    if (CurrentMillis - PreviousMllis >= 5000) {
	PreviousMllis = CurrentMillis;
	mysysinfo si;
	getSysInfo(&si);

	float f_load = (float) si.loads[0] / (1 << SI_LOAD_SHIFT);
	unsigned cload = f_load * 100.f;
	//printf("\033[1;33m  Total Ram: \033[0;m %ldk \t Free: %ldk \n", si.totalram / 1024, si.freeram / 1024);
	disk_info di[2]; //TODO size
	int disks = getDisksInfo(di, 2);

	unsigned freeram = si.freeram / 1024 / 1024;
	unsigned totalram = si.totalram / 1024 / 1024;
	totalram = (freeram * 100) / totalram;

	char buf[32];
	LCDclear();
	snprintf(buf, sizeof(buf), "MF:%uM %u%%", freeram, totalram);

	LCDdrawstring(0, 0, buf);
	snprintf(buf, sizeof(buf), "CPU:%3.1u%% %3.uC", cload, getCPUTemp());
	LCDdrawstring(0, 8, buf);
	for (int i = 0; i < disks; i++) {
	    snprintf(buf, sizeof(buf), "%s", di[i].mount);
	    LCDdrawstring(0, 16 * i + 16, buf);
	    snprintf(buf, sizeof(buf), "A%s F%s", di[i].size, di[i].free);
	    LCDdrawstring(0, 16 * i + 24, buf);
	}
	LCDdisplay();
    }
}

void PrintMainScreen2();

void PrintMainScreen1()
{
    PreviousMllis = 0;
    DoSomething = drawMainScreen1;
    Buttons[KEY_SHORT].OnPress = PrintMainScreen2;
    Buttons[KEY_LONG].OnPress = StopAndPrintMenu;
}

void drawMainScreen2()
{
    unsigned long CurrentMillis = millis();
    if (CurrentMillis - PreviousMllis >= 5000) {
	PreviousMllis = CurrentMillis;
	mysysinfo si;
	getSysInfo(&si);

	unsigned days = si.uptime / 86400;
	unsigned hours = (si.uptime / 3600) - (days * 24);
	unsigned mins = (si.uptime / 60) - (days * 1440) - (hours * 60);

	struct ifaddrs *ifAddrStruct = NULL;
	struct ifaddrs *ifa = NULL;
	void *tmpAddrPtr = NULL;
	uint8_t line = 1;
	getifaddrs(&ifAddrStruct);

	char buf[32];
	LCDclear();
	snprintf(buf, sizeof(buf), "UPt:%.1u:%2.2u:%2.2u", days, hours, mins);
	LCDdrawstring(0, 0, buf);

	for (ifa = ifAddrStruct; ifa != NULL; ifa = ifa->ifa_next) {
	    // Ignore not connected
	    if (ifa->ifa_addr == NULL)
		continue;

	    // Ignore Localhost
	    if (strcmp(ifa->ifa_name, "lo") == 0)
		continue;

	    // Show only IPv4
	    if (ifa->ifa_addr->sa_family == AF_INET) {
		tmpAddrPtr = &((struct sockaddr_in *) ifa->ifa_addr)->sin_addr;
		char addressBuffer[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, tmpAddrPtr, addressBuffer, INET_ADDRSTRLEN);
		//printf("'%s': %s\n", ifa->ifa_name, addressBuffer);
		LCDdrawstring(5, line * 16, ifa->ifa_name);
		LCDdrawstring(0, line * 16 + 8, addressBuffer);
		line++;
	    }
	    /*
	     // Show IPv6
	     else if (ifa->ifa_addr->sa_family == AF_INET6)
	     {
	     tmpAddrPtr = &((struct sockaddr_in6 *)ifa->ifa_addr)->sin6_addr;
	     char addressBuffer[INET6_ADDRSTRLEN];
	     inet_ntop(AF_INET6, tmpAddrPtr, addressBuffer, INET6_ADDRSTRLEN);
	     printf("'%s': %s\n", ifa->ifa_name, addressBuffer);
	     }
	     */
	}
	LCDdisplay();
	if (ifAddrStruct != NULL)
	    freeifaddrs(ifAddrStruct); //remember to free ifAddrStruct
    }
}

void PrintMainScreen2()
{
    PreviousMllis = 0;
    DoSomething = drawMainScreen2;
    Buttons[KEY_SHORT].OnPress = PrintMainScreen1;
    Buttons[KEY_LONG].OnPress = StopAndPrintMenu;
}

void PrintMainMenu()
{
    Buttons[KEY_LONG].OnPress = RunSelected;
    Buttons[KEY_SHORT].OnPress = MenuDown;

    CurrentMenu = &MainMenu;
    ActiveMenuItems = sizeof(MainMenu) / sizeof(MenuItem);
    ActiveMenuItem = 1;
    DrawMenu();
}

void DoShutdown()
{
    LCDclear();
    LCDdrawstring(0, 0, "System\nshutdown...");
    LCDdisplay();
    system("shutdown -P now");
    exit(0);
}

void DoReboot()
{
    LCDclear();
    LCDdrawstring(0, 0, "System\nRebooting...");
    LCDdisplay();
    system("reboot");
    exit(0);
}

void PrintPowerSettings()
{
    Buttons[KEY_LONG].OnPress = RunSelected;
    Buttons[KEY_SHORT].OnPress = MenuDown;

    CurrentMenu = &PowerSettingsMenu;
    ActiveMenuItems = sizeof(PowerSettingsMenu) / sizeof(MenuItem);
    ActiveMenuItem = 1;
    DrawMenu();
}

void DrawClock()
{
    unsigned long CurrentMillis = millis();

    if (CurrentMillis - PreviousMllis >= 1000) {
	printf("DrawClock() in %lu\n", CurrentMillis);
	PreviousMllis = CurrentMillis;
	char buffer[32];
	time_t rawtime;
	time(&rawtime);
	struct tm *timeinfo = localtime(&rawtime);

	LCDclear();
	LCDdrawstring(0, 0, (*CurrentMenu)[ActiveMenuItem - 1].Name);
	LCDdrawline(0, 8, LCDWIDTH, 8, BLACK);

	strftime(buffer, 32, "%d/%m/%Y", timeinfo);
	LCDdrawstring(10, 10, buffer); // Print date DD/MM/YYYY
	strftime(buffer, 32, "%H:%M:%S", timeinfo);
	LCDdrawstring(15, 26, buffer); // print time HH:MM:SS

	LCDdisplay();
    }
}

void StopAndPrintMenu()
{
    DoSomething = DoNothing;
    PrintMainMenu();
}

void ShowTime()
{
    DoSomething = DrawClock;

    Buttons[KEY_SHORT].OnPress = DoNothing; // TODO: Temporary: Press any key for update screen
    Buttons[KEY_LONG].OnPress = StopAndPrintMenu; // Return to previous menu and reset button controls
}

void ShowLogo()
{
    Buttons[KEY_SHORT].OnPress = DoNothing;
    Buttons[KEY_LONG].OnPress = PrintMainMenu;

    LCDshowLogo(BootLogo);	// No need to clear display and draw image.
}

int main()
{
    printf("Program started...\n");
    signal(SIGINT, intHandler);
    signal(SIGTERM, intHandler);

    DoSomething = DoNothing;

    if (wiringPiSetup() == -1)
	return 1;

    // LCD Init: CLK, DIN, DC, CS, RST, Contrast (Max: 127)
    LCDInit(LCD_PINS, LCD_CONTRAST);
    LCDshowLogo(BootLogo);
    // TODO: Wait until system fully started (Boot logo)
    delay(1000);

    // Init buttons
    pinMode(BTN_PIN, INPUT);
    pullUpDnControl(BTN_PIN, PUD_UP);

//	PrintMainMenu();
    PrintMainScreen1();

    unsigned ri=0;
    while (keepRunning) {
	if(ri++ > 20){
	    ri=0;
	    LCDInit(LCD_PINS, LCD_CONTRAST);
	}
	DoSomething();

	for (int i = 0; i < 100; i++) {
	    if (digitalRead(BTN_PIN) == LOW) {
		// Wait some time for debounce
		delay(25);
		// Read again
		unsigned cnt = 0;
		int key = -1;
		while (digitalRead(BTN_PIN) == LOW && keepRunning) {
		    key = KEY_SHORT;
		    cnt++;
		    delay(50);
		    // Still pressed, wait for unhold
		    if (cnt > 20) {
			key = KEY_LONG;
			break;
		    }
		}
//		    printf("cnt:%d  key:%d\n", cnt, key);
		if (key >= 0) {
		    Buttons[key].OnPress();
		    DoSomething();
		    while (digitalRead(BTN_PIN) == LOW && keepRunning) {
			delay(50);
		    }
		    delay(100);
		}
	    }
	    delay(10);
	}

    }

    LCDclear();
    LCDdrawstring(20, 16, "Goodbye!");
    LCDdisplay();
}
