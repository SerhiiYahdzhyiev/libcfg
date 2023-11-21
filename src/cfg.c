#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include "../include/cfg.h"

/**
 * @brief frees the config buffer
 * @param cfg pointer to the cfg object
*/
void cfg_free(cfg_t* cfg) {
    cfg_setting_t* current;

    for (size_t i = 0; i < cfg->settings_len; ++i) {
        current = cfg->settings[i];

        if (current->type == cfg_setting_type_string) {
            free(current->string);
        }

        free(current->identifier);
        free(current);
    }

    free(cfg->settings);
}

void cfg_dump(cfg_t* cfg) {
    cfg_setting_t* current;

    for (size_t i = 0; i < cfg->settings_len; ++i) {
        current = cfg->settings[i];

        switch (current->type) {
            case cfg_setting_type_string: {
                printf("%s=%s\n", current->identifier, current->string);
                break;
            }
            case cfg_setting_type_integer: {
                printf("%s=%lld\n", current->identifier, current->integer);
                break;
            }
            case cfg_setting_type_boolean: {
                printf("%s=%s\n", current->identifier, current->boolean ? "true" : "false");
                break;
            }
            case cfg_setting_type_decimal: {
                printf("%s=%llf\n", current->identifier, current->decimal);
                break;
            }
            
            default:
                break;
        }
    }
}

int cfg_add_setting(cfg_t* cfg, cfg_setting_t* setting) {
    if (cfg->settings_len == 0) {
        cfg->settings = malloc(sizeof(cfg_setting_t*) * (cfg->settings_len + 1)); // todo: handle error case
    } else {
        cfg->settings = realloc(cfg->settings, sizeof(cfg_setting_t*) * (cfg->settings_len + 1)); // todo: handle error case
    }
    
    cfg->settings_len += 1;
    cfg->settings[cfg->settings_len - 1] = setting;

    return 0;
}

int cfg_add_string_setting(cfg_t* cfg, const char* string, const char* identifier) {
    cfg_setting_t* setting = malloc(sizeof(cfg_setting_t));

    setting->type = cfg_setting_type_string;
    setting->identifier = strdup(identifier);
    setting->string = strdup(string);

    if (cfg_add_setting(cfg, setting) != 0) {
        free(setting->string);
        free(setting->identifier);
        free(setting);
        return 1;
    }

    return 0;
}

int cfg_add_string_setting_with_length(cfg_t* cfg, const char* str, size_t str_len, const char* id, size_t id_len) {
    cfg_setting_t* setting = malloc(sizeof(cfg_setting_t));

    setting->type = cfg_setting_type_string;
    setting->identifier = strndup(id, id_len);
    setting->string = strndup(str, str_len);

    if (cfg_add_setting(cfg, setting) != 0) {
        free(setting->string);
        free(setting->identifier);
        free(setting);
        return 1;
    }

    return 0;
}

int cfg_parse(cfg_t* cfg, const char* str, off_t len) {
    size_t c1 = 0;
    size_t c2 = 0;
    size_t id_pos = 0;
    size_t id_len = 0;
    size_t value_pos = 0;
    size_t value_len = 0;

    while (c2 < len) {
        switch (str[c1]) {
            /* forward the cursor until something meaningful */
            case '\r':
            case '\t':
            case ' ': {
                c2 += 1;
            }
            /* end of line expect an identifier */
            case '\n': {
                c2 += 1;
                if (c2 >= len) {
                    return 1;
                }
                c1 = c2;
                break;
            }
            /* forward the cursor to the end of the line */
            case '#': {
                while (str[c2] != '\n' && c2 < len) {
                    c2 += 1;
                }
                c1 = c2;
                break;
            }
            /* we have an identifier */
            default: {
                /* get the position and length of the identifier */
                while (str[c2] != '=' && c2 < len) {
                    c2 += 1;
                }
                id_pos = c1;
                id_len = c2 - c1;

                /* forward to the value */
                c2 += 1;
                if (c2 >= len) {
                    return 1;
                }
                c1 = c2;

                /* get the position and length of the value */
                while (str[c2] != '\n' && str[c2] != '#' && c2 < len) {
                    c2 += 1;
                }
                value_pos = c1;
                value_len = c2 - c1;

                switch (str[value_pos]) {
                    /* the value is a number */
                    case '-':
                    case '0':
                    case '1':
                    case '2':
                    case '3':
                    case '4':
                    case '5':
                    case '6':
                    case '7':
                    case '8':
                    case '9': {
                        if (cfg_add_string_setting_with_length(cfg, &str[value_pos], value_len, &str[id_pos], id_len)) {
                            return 1;
                        }                        
                        break;
                    }
                    /* the value is a string */
                    case '\"': {
                        if (cfg_add_string_setting_with_length(cfg, &str[value_pos], value_len, &str[id_pos], id_len)) {
                            return 1;
                        }                        
                        break;
                    }
                    /* the value is a */
                    case 'f':
                    case 't': {
                        if (cfg_add_string_setting_with_length(cfg, &str[value_pos], value_len, &str[id_pos], id_len)) {
                            return 1;
                        }
                        break;
                    }
                    default: {
                        return 1;
                    }
                }

                c1 = c2;
                break;
            }
        }
    }

    return 0;
}

/**
 * @brief Gets file size in bytes
 * @param fd File descriptor
 * @return Size of file in bytes
 */
size_t cfg_get_file_size(int fd) {
    struct stat s;

    fstat(fd, &s);
    return s.st_size;
}

/**
 * @brief loads a supported config file into the program
 * @param cfg pointer to a cfg object (preferably initialized on the stack)
 * @param path path to the config file
 * @returns 0 on success
*/
int cfg_load(cfg_t* cfg, const char* path) {
    int status = 0;
    int fd;
    off_t raw_len;
    char *raw_ptr;

    fd = open(path, O_RDWR, 0600);
    if (fd == -1) {
        return 1;
    }

    if (cfg_get_file_size(fd) == 0) {
        status = 1;
        goto cfg_load_close_fd;
    }

    raw_len = lseek(fd, 0, SEEK_END);
    if (raw_len == -1) {
        status = 1;
        goto cfg_load_close_fd;
    }

    raw_ptr = mmap(0, raw_len, PROT_READ, MAP_PRIVATE, fd, 0);
    if (raw_ptr == MAP_FAILED) {
        status = 1;
        goto cfg_load_close_fd;
    }

    if (cfg_parse(cfg, raw_ptr, raw_len) != 0) {
        status = 1;
        cfg_free(cfg);
    }

cfg_load_unmap:
    munmap(raw_ptr, raw_len);

cfg_load_close_fd:
    close(fd);

    return status;
}