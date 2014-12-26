#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <gccore.h>
#include <wiiuse/wpad.h>
#include <bte/bte.h>
#include <bte/bd_addr.h>

#define VID  0x054C
#define PID  0x0268

/*
config header:
	offset  |  size  |  value
	   0    |   4    |  number of paired devices
	   4    |   6    |  BT MAC of the 1st device
	  4+6*i |   6    |  BT MAC of the i-th device
*/
static const char conf_file[] ATTRIBUTE_ALIGN(32) = "/shared2/sys/ds3wiibt.dat";


int run = 1, bd_addr_read = 0;
int heap_id = -1;
void *xfb = NULL;
GXRModeObj *rmode = NULL;
void init_video();
void print_bd_addr(struct bd_addr *bdaddr);
void print_mac(uint8_t *mac);
int bte_read_bdaddr_cb(s32 result, void *userdata);
struct bd_addr bdaddr;

void find_and_set_mac();
int ps3_get_bd_mac(int fd, uint8_t *mac);
int ps3_get_pair_mac(int fd, uint8_t *mac);
int ps3_set_pair_mac(int fd, const uint8_t *mac);
int config_add_mac(const uint8_t *mac);

int main(int argc, char **argv)
{
	init_video();
	WPAD_Init();
    
    heap_id = iosCreateHeap(4096);
    
    printf("sixpair Wii version by xerpi\n");
    printf("Connect a PS3 controller to the USB and press A.  Press HOME to exit.\n\n");
    
    while(run) {
        WPAD_ScanPads();
        u32 pressed = WPAD_ButtonsDown(0);
        if (pressed & WPAD_BUTTON_A) {
            find_and_set_mac();
        }
        
        if (pressed & WPAD_BUTTON_HOME) run = 0;
        VIDEO_WaitVSync();
    }
	exit(0);
	return 0;
}

void find_and_set_mac()
{
    bd_addr_read = 0;
    BTE_ReadBdAddr(&bdaddr, bte_read_bdaddr_cb);
    
    //Wait until we get the callback...
    while (!bd_addr_read) usleep(1000);
        
    //Find PS3 USB controller
    usb_device_entry dev_entry[8];
    unsigned char dev_count;
    if (USB_GetDeviceList(dev_entry, 8, USB_CLASS_HID, &dev_count) < 0) {
        printf("Error getting USB device list.\n"); return;
    }
        
    int i;
    usb_device_entry *de;
    for (i = 0; i < dev_count; ++i) {
        de = &dev_entry[i];
        
        if( de->vid == VID && de->pid == PID) {
            printf("PS3 controller found.\n");
            
            int fd;
            if (USB_OpenDevice(de->device_id, VID, PID, &fd) < 0) {
                printf("Could not open the PS3 controller.\n"); return;
            }
        
            printf("Wii local BD MAC: ");
            print_mac(bdaddr.addr);
            printf("\n");
            
            uint8_t mac[6] ATTRIBUTE_ALIGN(32);
            printf("Controller's bluetooth MAC address: ");
            ps3_get_bd_mac(fd, mac);
            print_mac(mac);
            printf("\n");
			//Write the MAC to the NAND
			if (config_add_mac(mac)) {
				printf("MAC added to the config file!\n");
			}
            
                    
            ps3_get_pair_mac(fd, mac);
            printf("\nCurrent controller paired address: ");
            print_mac(mac);
            
            struct bd_addr bd2;
            memcpy(bd2.addr, mac, sizeof(uint8_t) * 6);
            if (bd_addr_cmp(&bdaddr, &bd2)) {
                printf("\n\nAddress is already set! Press HOME to exit.\n");
                return;
            }       
            
            print_mac(mac);
            printf("\nSetting the pair address...");
            
            uint8_t *mac2 = bdaddr.addr;
            ps3_set_pair_mac(fd, mac2);
            ps3_get_pair_mac(fd, mac);
            printf("\nController's pair address set to: ");
            print_mac(mac);
            
            memcpy(bd2.addr, mac2, sizeof(uint8_t) * 6);
            if (bd_addr_cmp(&bdaddr, &bd2)) {
                printf("\n\nAddress set correctly! Press HOME to exit.\n");   
            } else {
                printf("\n\nPair MAC Address could not be set correctly.\n");   
            }
            
            USB_CloseDevice(&fd);
            return;
        }
    }
    printf("No controller found on USB busses.\n");
}

//Deletes the config file and adds this mac
int config_add_mac(const uint8_t *mac)
{
	ISFS_Initialize();
	int fd = ISFS_Open(conf_file, ISFS_OPEN_RW);
	if (fd >= 0) {
		ISFS_Close(fd);
		ISFS_Delete(conf_file);
	}
	printf("Creating config file... ");
	int ret = ISFS_CreateFile(conf_file, 0, ISFS_OPEN_RW, ISFS_OPEN_RW, ISFS_OPEN_RW);
	if (ret < 0) {
		printf("Error creating \"%s\" : %d\n", conf_file, ret);
		return -1;
	}
	printf("done!\n");
	fd = ISFS_Open(conf_file, ISFS_OPEN_RW);
	ISFS_Seek(fd, 0, SEEK_SET);
	int n = ISFS_Write(fd, mac, 6);
	ISFS_Close(fd);
	ISFS_Deinitialize();
	return (n == 6);
}

int ps3_get_bd_mac(int fd, uint8_t *mac)
{
    uint8_t ATTRIBUTE_ALIGN(32) msg[17];
    int ret = USB_WriteCtrlMsg(fd,
                USB_REQTYPE_INTERFACE_GET,
                USB_REQ_GETREPORT,
                (USB_REPTYPE_FEATURE<<8) | 0xf2,
                0,
                sizeof(msg),
                msg);
    
    mac[0] = msg[4];
    mac[1] = msg[5];
    mac[2] = msg[6];
    mac[3] = msg[7];
    mac[4] = msg[8];
    mac[5] = msg[9];
    return ret;
}

int ps3_get_pair_mac(int fd, uint8_t *mac)
{
    uint8_t ATTRIBUTE_ALIGN(32) msg[8];
    int ret = USB_WriteCtrlMsg(fd,
                USB_REQTYPE_INTERFACE_GET,
                USB_REQ_GETREPORT,
                (USB_REPTYPE_FEATURE<<8) | 0xf5,
                0,
                sizeof(msg),
                msg);

    mac[0] = msg[2];
    mac[1] = msg[3];
    mac[2] = msg[4];
    mac[3] = msg[5];
    mac[4] = msg[6];
    mac[5] = msg[7];
    return ret;
}

int ps3_set_pair_mac(int fd, const uint8_t *mac)
{
    uint8_t ATTRIBUTE_ALIGN(32) msg[] = {0x01, 0x00, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]};
    int ret = USB_WriteCtrlMsg(fd,
                USB_REQTYPE_INTERFACE_SET,
                USB_REQ_SETREPORT,
                (USB_REPTYPE_FEATURE<<8) | 0xf5,
                0,
                sizeof(msg),
                msg);
    return ret;
}

int bte_read_bdaddr_cb(s32 result, void *userdata)
{
    bd_addr_read = 1;
    return 1;
}

void print_mac(uint8_t *mac)
{
    printf("%02X:%02X:%02X:%02X:%02X:%02X",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
}

void init_video()
{
	VIDEO_Init();
	rmode = VIDEO_GetPreferredMode(NULL);
	xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
	console_init(xfb,20,20,rmode->fbWidth,rmode->xfbHeight,rmode->fbWidth*VI_DISPLAY_PIX_SZ);
	VIDEO_Configure(rmode);
	VIDEO_SetNextFramebuffer(xfb);
	VIDEO_SetBlack(FALSE);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	if(rmode->viTVMode&VI_NON_INTERLACE) VIDEO_WaitVSync();
	printf("\x1b[1;0H");
}
