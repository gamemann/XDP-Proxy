#include <loader/utils/config.h>

/**
 * Loads the config from the file system.
 * 
 * @param cfg A pointer to the config structure.
 * @param cfg_file The path to the config file.
 * @param overrides Overrides to use instead of config values.
 * 
 * @return 0 on success or 1 on error.
 */
int load_config(config__t *cfg, const char* cfg_file, config_overrides_t* overrides)
{
    int ret;
    
    FILE *file = NULL;
    
    // Open config file.
    if ((ret = open_cfg(&file, cfg_file)) != 0 || file == NULL)
    {
        fprintf(stderr, "Error opening config file.\n");
        
        return ret;
    }

    set_cfg_defaults(cfg);

    memset(cfg->rules, 0, sizeof(cfg->rules));

    char* buffer = NULL;

    // Read config.
    if ((ret = read_cfg(file, &buffer)) != 0)
    {
        fprintf(stderr, "Error reading config file.\n");

        close_cfg(file);

        return ret;
    }

    // Parse config.
    if ((ret = parse_cfg(cfg, buffer, overrides)) != 0)
    {
        fprintf(stderr, "Error parsing config file.\n");

        close_cfg(file);

        return ret;
    }

    free(buffer);

    if ((ret = close_cfg(file)) != 0)
    {
        fprintf(stderr, "Error closing config file.\n");

        return ret;
    }

    return EXIT_SUCCESS;
}

/**
 * Opens the config file.
 * 
 * @param file_name Path to config file.
 * 
 * @return 0 on success or 1 on error.
 */
int open_cfg(FILE** file, const char *file_name)
{
    // Close any existing files.
    if (*file != NULL)
    {
        fclose(*file);

        *file = NULL;
    }

    *file = fopen(file_name, "r");

    if (*file == NULL)
    {
        return 1;
    }

    return 0;
}

/**
 * Close config file.
 * 
 * @param file A pointer to the file to close.
 * 
 * @param return 0 on success or error value of fclose().
 */
int close_cfg(FILE* file)
{
    return fclose(file);
}

/**
 * Reads contents from the config file.
 * 
 * @param file The file pointer.
 * @param buffer The buffer to store the data in (manually allocated).
 */
int read_cfg(FILE* file, char** buffer)
{
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    rewind(file);

    if (file_size <= 0)
    {
        return 1;
    }

    *buffer = malloc(file_size + 1);

    if (*buffer == NULL)
    {
        return 1;
    }

    size_t read = fread(*buffer, 1, file_size, file);
    (*buffer)[read] = '\0';

    return 0;
}

/**
 * Read the config file and stores values in config structure.
 * 
 * @param cfg A pointer to the config structure.
 * @param data The config data.
 * @param overrides Overrides to use instead of config values.
 * 
 * @return 0 on success or 1/-1 on error.
 */
int parse_cfg(config__t *cfg, const char* data, config_overrides_t* overrides)
{
    // Initialize config.
    config_t conf;
    config_setting_t *setting;

    config_init(&conf);

    // Attempt to read the config.
    if (config_read_string(&conf, data) == CONFIG_FALSE)
    {
        log_msg(cfg, 0, 1, "Error from LibConfig when reading file - %s (Line %d)", config_error_text(&conf), config_error_line(&conf));

        config_destroy(&conf);

        return EXIT_FAILURE;
    }

    int verbose;

    if (config_lookup_int(&conf, "verbose", &verbose) == CONFIG_TRUE || (overrides && overrides->verbose > -1))
    {
        if (overrides && overrides->verbose > -1)
        {
            cfg->verbose = overrides->verbose;
        }
        else
        {
            cfg->verbose = verbose;
        }
    }

    const char* log_file;

    if (config_lookup_string(&conf, "log_file", &log_file) == CONFIG_TRUE || (overrides && overrides->log_file != NULL))
    {
        // We must free previous value to prevent memory leak.
        if (cfg->log_file != NULL)
        {
            free(cfg->log_file);
            cfg->log_file = NULL;
        }

        if (overrides && overrides->log_file != NULL)
        {
            if (strlen(overrides->log_file) > 0)
            {
                cfg->log_file = strdup(overrides->log_file);
                
            }
            else
            {
                cfg->log_file = NULL;
            }
        }
        else
        {
            if (strlen(log_file) > 0)
            {
                cfg->log_file = strdup(log_file);
            }
            else
            {
                cfg->log_file = NULL;
            }
        }
    }

    // Get interface(s).
    config_setting_t* interfaces = config_lookup(&conf, "interface");

    if (interfaces)
    {
        if (config_setting_is_list(interfaces))
        {
            for (int i = 0; i < config_setting_length(interfaces); i++)
            {
                if (i >= MAX_INTERFACES)
                {
                    break;
                }

                const char* interface = config_setting_get_string_elem(interfaces, i);

                if (!interface)
                {
                    continue;
                }

                if (cfg->interfaces[i])
                {
                    free(cfg->interfaces[i]);
                    cfg->interfaces[i] = NULL;
                }

                if (i == 0 && overrides && overrides->interface)
                {
                    cfg->interfaces[i] = strdup(overrides->interface);
                }
                else
                {
                    cfg->interfaces[i] = strdup(interface);
                }

                cfg->interfaces_cnt++;
            }
        }
        else
        {
            const char* interface;

            if (config_lookup_string(&conf, "interface", &interface) == CONFIG_TRUE)
            {
                if (cfg->interfaces[0])
                {
                    free(cfg->interfaces[0]);
                    cfg->interfaces[0] = NULL;
                }

                if (overrides && overrides->interface)
                {
                    cfg->interfaces[0] = strdup(overrides->interface);
                }
                else
                {
                    cfg->interfaces[0] = strdup(interface);
                }

                cfg->interfaces_cnt = 1;
            }
        }
    }
    else if (overrides && overrides->interface)
    {
        if (cfg->interfaces[0])
        {
            free(cfg->interfaces[0]);
            cfg->interfaces[0] = NULL;
        }

        cfg->interfaces[0] = strdup(overrides->interface);
        cfg->interfaces_cnt = 1;
    }

    // Pin BPF maps.
    int pin_maps;

    if (config_lookup_bool(&conf, "pin_maps", &pin_maps) == CONFIG_TRUE || (overrides && overrides->pin_maps > -1))
    {
        if (overrides && overrides->pin_maps > -1)
        {
            cfg->pin_maps = overrides->pin_maps;
        }
        else
        {
            cfg->pin_maps = pin_maps;
        }
    }

    // Get auto update time.
    int update_time;

    if (config_lookup_int(&conf, "update_time", &update_time) == CONFIG_TRUE || (overrides && overrides->update_time > -1))
    {
        if (overrides && overrides->update_time > -1)
        {
            cfg->update_time = overrides->update_time;
        }
        else
        {
            cfg->update_time = update_time;
        }
    }

    // Get no stats.
    int no_stats;

    if (config_lookup_bool(&conf, "no_stats", &no_stats) == CONFIG_TRUE || (overrides && overrides->no_stats > -1))
    {
        if (overrides && overrides->no_stats > -1)
        {
            cfg->no_stats = overrides->no_stats;
        }
        else
        {
            cfg->no_stats = no_stats;
        }
    }

    // Stats per second.
    int stats_per_second;

    if (config_lookup_bool(&conf, "stats_per_second", &stats_per_second) == CONFIG_TRUE || (overrides && overrides->stats_per_second > -1))
    {
        if (overrides && overrides->stats_per_second > -1)
        {
            cfg->stats_per_second = overrides->stats_per_second;
        }
        else
        {
            cfg->stats_per_second = stats_per_second;
        }
    }

    // Get stdout update time.
    int stdout_update_time;

    if (config_lookup_int(&conf, "stdout_update_time", &stdout_update_time) == CONFIG_TRUE || (overrides && overrides->stdout_update_time > -1))
    {
        if (overrides && overrides->stdout_update_time > -1)
        {
            cfg->stdout_update_time = overrides->stdout_update_time;
        }
        else
        {
            cfg->stdout_update_time = stdout_update_time;
        }
    }

    // Read forward rules.
    setting = config_lookup(&conf, "rules");

    if (setting && config_setting_is_list(setting))
    {
        for (int i = 0; i < config_setting_length(setting); i++)
        {
            fwd_rule_cfg_t* rule = &cfg->rules[i];

            config_setting_t* rule_cfg = config_setting_get_elem(setting, i);

            if (rule == NULL || rule_cfg == NULL)
            {
                log_msg(cfg, 0, 1, "[WARNING] Failed to read forward rule at index #%d. 'rule' or 'rule_cfg' is NULL (make sure you didn't exceed the maximum rules allowed!)...");

                continue;
            }

            rule->set = 1;
            cfg->rules_cnt++;

            // Enabled.
            int enabled;

            if (config_setting_lookup_bool(rule_cfg, "enabled",  &enabled) == CONFIG_TRUE)
            {
                rule->enabled = enabled;
            }

            // Log.
            int log;

            if (config_setting_lookup_bool(rule_cfg, "log", &log) == CONFIG_TRUE)
            {
                rule->log = log;
            }

            // Protocol.
            const char* protocol;

            if (config_setting_lookup_string(rule_cfg, "protocol", &protocol) == CONFIG_TRUE)
            {
                if (rule->protocol)
                {
                    free((void*)rule->protocol);

                    rule->protocol = NULL;
                }

                rule->protocol = strdup(protocol);
            }

            // Bind IP.
            const char* bind_ip;

            if (config_setting_lookup_string(rule_cfg, "bind_ip", &bind_ip) == CONFIG_TRUE)
            {
                if (rule->bind_ip)
                {
                    free((void*)rule->bind_ip);

                    rule->bind_ip = NULL;
                }

                rule->bind_ip = strdup(bind_ip);
            }

            // Bind port.
            int bind_port;

            if (config_setting_lookup_int(rule_cfg, "bind_port", &bind_port) == CONFIG_TRUE)
            {
                rule->bind_port = bind_port;
            }

            // Destination IP.
            const char* dst_ip;

            if (config_setting_lookup_string(rule_cfg, "dst_ip", &dst_ip) == CONFIG_TRUE)
            {
                if (rule->dst_ip)
                {
                    free((void*)rule->dst_ip);

                    rule->dst_ip = NULL;
                }

                rule->dst_ip = strdup(dst_ip);
            }

            // Destination port.
            int dst_port;

            if (config_setting_lookup_int(rule_cfg, "dst_port", &dst_port) == CONFIG_TRUE)
            {
                rule->dst_port = dst_port;
            }
        }
    }

    config_destroy(&conf);

    return EXIT_SUCCESS;
}

/**
 * Saves config to file system.
 * 
 * @param cfg A pointer to the config.
 * @param file_path The file path to store the config into.
 * 
 * @param return 0 on success or 1 on failure.
 */
int save_cfg(config__t* cfg, const char* file_path)
{
    config_t conf;
    config_setting_t *root, *setting;

    FILE* file;

    config_init(&conf);
    root = config_root_setting(&conf);

    // Add verbose.
    setting = config_setting_add(root, "verbose", CONFIG_TYPE_INT);
    config_setting_set_int(setting, cfg->verbose);

    // Add log file.
    if (cfg->log_file)
    {
        setting = config_setting_add(root, "log_file", CONFIG_TYPE_STRING);
        config_setting_set_string(setting, cfg->log_file);
    }

    // Add interface(s).
    if (cfg->interfaces_cnt > 0)
    {
        if (cfg->interfaces_cnt > 1)
        {
            setting = config_setting_add(root, "interface", CONFIG_TYPE_LIST);

            for (int i = 0; i < cfg->interfaces_cnt; i++)
            {
                const char* interface = cfg->interfaces[i];

                if (!interface)
                {
                    continue;
                }

                config_setting_t* setting_interface = config_setting_add(setting, NULL, CONFIG_TYPE_STRING);
                config_setting_set_string(setting_interface, interface);
            }
        }
        else
        {
            const char* interface = cfg->interfaces[0];

            if (interface)
            {
                setting = config_setting_add(root, "interface", CONFIG_TYPE_STRING);
                config_setting_set_string(setting, interface);
            }
        }
    }

    // Add pin maps.
    setting = config_setting_add(root, "pin_maps", CONFIG_TYPE_BOOL);
    config_setting_set_bool(setting, cfg->pin_maps);

    // Add update time.
    setting = config_setting_add(root, "update_time", CONFIG_TYPE_INT);
    config_setting_set_int(setting, cfg->update_time);

    // Add no stats.
    setting = config_setting_add(root, "no_stats", CONFIG_TYPE_BOOL);
    config_setting_set_bool(setting, cfg->no_stats);

    // Add stats per second.
    setting = config_setting_add(root, "stats_per_second", CONFIG_TYPE_BOOL);
    config_setting_set_bool(setting, cfg->stats_per_second);

    // Add stdout update time.
    setting = config_setting_add(root, "stdout_update_time", CONFIG_TYPE_INT);
    config_setting_set_int(setting, cfg->stdout_update_time);

    // Add forward rules.
    config_setting_t* rules = config_setting_add(root, "rules", CONFIG_TYPE_LIST);

    if (rules)
    {
        for (int i = 0; i < MAX_FWD_RULES; i++)
        {
            fwd_rule_cfg_t* rule = &cfg->rules[i];

            if (!rule->set)
            {
                continue;
            }

            config_setting_t* rule_cfg = config_setting_add(rules, NULL, CONFIG_TYPE_GROUP);

            if (rule_cfg)
            {
                // Add enabled setting.
                config_setting_t* enabled = config_setting_add(rule_cfg, "enabled", CONFIG_TYPE_BOOL);
                config_setting_set_bool(enabled, rule->enabled);

                // Add log setting.
                config_setting_t* log = config_setting_add(rule_cfg, "log", CONFIG_TYPE_BOOL);
                config_setting_set_bool(log, rule->log);

                // Add protocol.
                if (rule->protocol)
                {
                    config_setting_t* protocol = config_setting_add(rule_cfg, "protocol", CONFIG_TYPE_STRING);
                    config_setting_set_string(protocol, rule->protocol);
                }

                // Add bind IP.
                if (rule->bind_ip)
                {
                    config_setting_t* bind_ip = config_setting_add(rule_cfg, "bind_ip", CONFIG_TYPE_STRING);
                    config_setting_set_string(bind_ip, rule->bind_ip);
                }

                // Add bind port.
                config_setting_t* bind_port = config_setting_add(rule_cfg, "bind_port", CONFIG_TYPE_INT);
                config_setting_set_int(bind_port, rule->bind_port);

                // Add destination IP.
                if (rule->dst_ip)
                {
                    config_setting_t* dst_ip = config_setting_add(rule_cfg, "dst_ip", CONFIG_TYPE_STRING);
                    config_setting_set_string(dst_ip, rule->dst_ip);
                }

                // Add destination port.
                config_setting_t* dst_port = config_setting_add(rule_cfg, "dst_port", CONFIG_TYPE_INT);
                config_setting_set_int(dst_port, rule->dst_port);
            }
        }
    }

    // Write config to file.
    file = fopen(file_path, "w");

    if (!file)
    {
        config_destroy(&conf);

        return 1;
    }

    config_write(&conf, file);

    fclose(file);
    config_destroy(&conf);

    return 0;
}

/**
 * Sets the default values for a forward rule.
 * 
 * @param rule A pointer to the forward rule.
 * 
 * @return void
 */
void set_fwd_rule_defaults(fwd_rule_cfg_t* rule)
{
    rule->set = 0;
    rule->enabled = 1;

    rule->log = 0;

    if (rule->bind_ip)
    {
        free((void*)rule->bind_ip);
    }

    rule->bind_ip = NULL;

    rule->bind_port = 0;

    if (rule->protocol)
    {
        free((void*)rule->protocol);
    }

    rule->protocol = NULL;

    if (rule->dst_ip)
    {
        free((void*)rule->dst_ip);
    }

    rule->dst_ip = NULL;

    rule->dst_port = 0;
}

/**
 * Sets the config structure's default values.
 * 
 * @param cfg A pointer to the config structure.
 * 
 * @return void
 */
void set_cfg_defaults(config__t* cfg)
{
    cfg->verbose = 2;
    cfg->log_file = strdup("/var/log/xdpfwd.log");
    cfg->update_time = 0;
    cfg->pin_maps = 1;
    cfg->no_stats = 0;
    cfg->stats_per_second = 0;
    cfg->stdout_update_time = 1000;

    cfg->interfaces_cnt = 0;

    for (int i = 0; i < MAX_INTERFACES; i++)
    {
        char* interface = cfg->interfaces[i];

        if (!interface)
        {
            continue;
        }

        free(interface);
        cfg->interfaces[i] = NULL;
    }

    cfg->rules_cnt = 0;

    for (int i = 0; i < MAX_FWD_RULES; i++)
    {
        fwd_rule_cfg_t* rule = &cfg->rules[i];

        set_fwd_rule_defaults(rule);
    }
}

/**
 * Prints a forward rule.
 * 
 * @param rule A pointer to the forward rule.
 * @param idx The current index.
 * 
 * @return void
 */
void print_fwd_rule(fwd_rule_cfg_t* rule, int idx)
{
    printf("\tRule #%d\n", idx);
    printf("\t\tEnabled => %d\n", rule->enabled);
    printf("\t\tLog => %d\n\n", rule->log);

    printf("\t\tBind IP => %s\n", rule->bind_ip);
    printf("\t\tBind Port => %d\n", rule->bind_port);
    printf("\t\tBind Protocol => %s\n\n", rule->protocol);

    printf("\t\tDestination IP => %s\n", rule->dst_ip);
    printf("\t\tDestination Port => %d\n", rule->dst_port);
}

/**
 * Prints config settings.
 * 
 * @param cfg A pointer to the config structure.
 * 
 * @return void
 */
void print_config(config__t* cfg)
{
    const char* log_file = "N/A";

    if (cfg->log_file != NULL)
    {
        log_file = cfg->log_file;
    }

    printf("Printing config...\n");
    printf("General Settings\n");
    
    printf("\tVerbose => %d\n", cfg->verbose);
    printf("\tLog File => %s\n", log_file);
    printf("\tPin BPF Maps => %d\n", cfg->pin_maps);
    printf("\tUpdate Time => %d\n", cfg->update_time);
    printf("\tNo Stats => %d\n", cfg->no_stats);
    printf("\tStats Per Second => %d\n", cfg->stats_per_second);
    printf("\tStdout Update Time => %d\n\n", cfg->stdout_update_time);

    printf("Interfaces\n");
    
    if (cfg->interfaces_cnt > 0)
    {
        for (int i = 0; i < cfg->interfaces_cnt; i++)
        {
            const char* interface = cfg->interfaces[i];
    
            if (!interface)
            {
                continue;
            }

            printf("\t- %s\n", interface);
        }

        printf("\n");
    }
    else
    {
        printf("\t- None\n\n");
    }

    printf("Rules\n");

    if (cfg->rules_cnt > 0)
    {
        for (int i = 0; i < cfg->rules_cnt; i++)
        {
            fwd_rule_cfg_t* rule = &cfg->rules[i];

            if (!rule || !rule->set)
            {
                continue;
            }
    
            print_fwd_rule(rule, i + 1);
    
            printf("\n\n");
        }
    }
    else
    {
        printf("\t- None\n");
    }
}

/**
 * Retrieves next available rule index.
 * 
 * @param cfg A pointer to the config structure.
 * 
 * @return The next available index or -1 if there are no available indexes.
 */
int get_next_available_fwd_rule_index(config__t* cfg)
{
    for (int i = 0; i < MAX_FWD_RULES; i++)
    {
        fwd_rule_cfg_t* rule = &cfg->rules[i];

        if (rule->set)
        {
            continue;
        }

        return i;
    }

    return -1;
}

/**
 * Retrieves index of a forward rule if it exists.
 * 
 * @param cfg A pointer to the config.
 * @param bind_ip The bind IP.
 * @param bind_port The bind port.
 * @param protocol The protocol.
 * 
 * @return The index of the forward rule (or -1 if it doesn't exist).
 */
int get_fwd_rule_index(config__t* cfg, const char* bind_ip, u16 bind_port, const char* protocol)
{
    char protocol_lower[64];
    strncpy(protocol_lower, protocol, sizeof(protocol_lower) - 1);
    protocol_lower[sizeof(protocol_lower) - 1] = '\0';

    lower_str(protocol_lower);

    for (int i = 0; i < MAX_FWD_RULES; i++)
    {
        fwd_rule_cfg_t* rule = &cfg->rules[i];

        if (!rule->bind_ip || !rule->protocol)
        {
            continue;
        }

        char rule_protocol_lower[64];
        strncpy(rule_protocol_lower, rule->protocol, sizeof(rule_protocol_lower) - 1);
        rule_protocol_lower[sizeof(rule_protocol_lower) - 1] = '\0';

        lower_str(rule_protocol_lower);

        if (strcmp(bind_ip, rule->bind_ip) != 0)
        {
            continue;
        }

        if (bind_port != rule->bind_port)
        {
            continue;
        }

        if (strcmp(protocol_lower, rule_protocol_lower) != 0)
        {
            continue;
        }

        return i;
    }

    return -1;
}