#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "plugin_api.h"

static char *g_lib_name = "libvjdN3250.so";
static char *g_plugin_purpose = "Check if the CRC-16-CCITT control sum of the file is equal to the given value (binary, decimal or hex)";
static char *g_plugin_author = "Vadim Dronov";

#define OPT_CRC16_STR "crc16"

static struct plugin_option g_po_arr[] = {
    {
        {
            OPT_CRC16_STR,
            required_argument,
            0, 0,
        },
        "Target value of control sum"
    }
};

static int g_po_arr_len = sizeof(g_po_arr)/sizeof(g_po_arr[0]);

//
// Private functions
//
static unsigned char reverse_table[16] =
{
  0x0, 0x8, 0x4, 0xC, 0x2, 0xA, 0x6, 0xE,
  0x1, 0x9, 0x5, 0xD, 0x3, 0xB, 0x7, 0xF
};

static unsigned char reverse_bits(unsigned char byte) {
  // Reverse the top and bottom nibbles then swap them.
  return (reverse_table[byte & 0b1111] << 4) | reverse_table[byte >> 4];
}

static unsigned short reverse_word(unsigned short word) {
  return ((reverse_bits(word & 0xFF) << 8) | reverse_bits(word >> 8));
}

//
// API functions
//
int plugin_get_info(struct plugin_info* ppi) {
    if (!ppi) {
        fprintf(stderr, "ERROR: invalid argument\n");
        return -1;
    }
    
    ppi->plugin_purpose = g_plugin_purpose;
    ppi->plugin_author = g_plugin_author;
    ppi->sup_opts_len = g_po_arr_len;
    ppi->sup_opts = g_po_arr;
    
    return 0;
}

int plugin_process_file(const char *fname,
        struct option in_opts[],
        size_t in_opts_len) {
	
	// Return error by default
    int ret = -1;
    
    char *DEBUG = getenv("LAB1DEBUG");
    
    if (!fname || !in_opts || !in_opts_len) {
        errno = EINVAL;
        return -1;
    }
    
    // Checking in_opts
    int got_option = 0;
    unsigned long int crc16_given = 0;
    unsigned short crc16_check = 0;
    for (size_t i = 0; i < in_opts_len; i++) {
    	if (!strcmp(in_opts[i].name, OPT_CRC16_STR)) {
			// Проверка на повтор опции
            if (got_option) {
		        fprintf(stderr, "ERROR: %s: Option '%s' was already supplied\n", \
		        		g_lib_name, in_opts[i].name);
				errno = EINVAL;
				return -1;
			}
			else {
				// Проверка первых двух символов – определяем систему счисления
				char *endptr = NULL;
				char c1 = ((char*)in_opts[i].flag)[0];
				char c2 = ((char*)in_opts[i].flag)[1];
				
				// Converting go decimal
				if (c1 == '0' && c2 == 'b')
					crc16_given = strtol((char*)in_opts[i].flag + 2, &endptr, 2);
				else if (c1 == '0' && c2 == 'x')
					crc16_given = strtol((char*)in_opts[i].flag, &endptr, 16);
				else
					crc16_given = strtol((char*)in_opts[i].flag, &endptr, 10);

				if (*endptr != '\0') {
					fprintf(stderr, "ERROR: %s: Failed to convert '%s'\n", \
							g_lib_name, (char*)in_opts[i].flag);
					errno = EINVAL;
					return -1;
				}
				
				// Максимальное значение - 0xffff
				if (crc16_given > 0xffff) {
					fprintf(stderr, "ERROR: %s: Input sum should be < 0xffff\n", g_lib_name);
					errno = EINVAL;
					return -1;
				}
				else
					crc16_check = (unsigned short)crc16_given;
				
				got_option = 1;
				if (DEBUG) fprintf(stderr, "DEBUG: %s: Given control sum: 0x%x\n", \
								g_lib_name, crc16_check);
			}
        }
        else {
        	errno = EINVAL;
            return -1;
        }
    }
    
    int saved_errno = 0;
    
    // Opening the file
    int fd = open(fname, O_RDONLY);
    if (fd < 0) return -1;
    
    FILE *fp = fdopen(fd, "r");
    if (!fp) {
    	saved_errno = errno;
    	goto END;
    }

    struct stat st = {0};
    int res = fstat(fd, &st);
    if (res < 0) {
        saved_errno = errno;
        goto END;
    }
    
    // Check that size of file is > 0
    if (st.st_size == 0) {
        if (DEBUG) {
            fprintf(stderr, "DEBUG: %s: File size should be > 0\n",
                g_lib_name);
        }
        saved_errno = ERANGE;
        goto END;
    }
    
    char *buf = NULL;
    size_t buf_len = 0;
    int nread;
    
    // Calculation of crc16_KERMIT
    unsigned short crc16_calc = 0x0000;
    while ((nread = getline(&buf, &buf_len, fp)) != -1) {
		char *block = buf;
		while (nread--) {
			crc16_calc ^= ((unsigned short)reverse_bits(*block++) << 8); // RefIn = true
			for (unsigned char i = 0; i < 8; i++)
		        crc16_calc = crc16_calc & 0x8000 ? (crc16_calc << 1) ^ 0x1021 : crc16_calc << 1;
		}
    }
    crc16_calc = reverse_word(crc16_calc); // RefOut = true
    if (buf) free(buf);
    
    if (DEBUG) fprintf(stderr, "DEBUG: %s: Calculated file crc16-CCIT sum: 0x%x\n", \
    				g_lib_name, crc16_calc);
    
    if (crc16_calc == crc16_check) {
    	if (DEBUG)
    		fprintf(stderr, "DEBUG: %s: Result 0x%x is equal to given value 0x%x\n", \
    				g_lib_name, crc16_calc, crc16_check);
    	ret = 0;
    }
    else {
    	if (DEBUG)
    		fprintf(stderr, "DEBUG: %s: Result 0x%x is unequal to given value 0x%x\n", \
    				g_lib_name, crc16_calc, crc16_check);
    	ret = 1;
	}

	// Закрытие файла
    END:
    if (fd) close(fd);
    if (fp) fclose(fp);
    errno = saved_errno;
    return ret;
}
