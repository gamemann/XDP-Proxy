#include <loader/utils/xdp.h>

/**
 * Finds a BPF map's FD.
 * 
 * @param prog A pointer to the XDP program structure.
 * @param mapname The name of the map to retrieve.
 * 
 * @return The map's FD.
 */
int get_map_fd(struct xdp_program *prog, const char *map_name)
{
    int fd = -1;

    struct bpf_object *obj = xdp_program__bpf_obj(prog);

    if (obj == NULL)
    {
        fprintf(stderr, "Error finding BPF object from XDP program.\n");

        goto out;
    }

    struct bpf_map *map = bpf_object__find_map_by_name(obj, map_name);

    if (!map) 
    {
        fprintf(stderr, "Error finding eBPF map: %s\n", map_name);

        goto out;
    }

    fd = bpf_map__fd(map);

    out:
        return fd;
}

/**
 * Custom print function for LibBPF that doesn't print anything (silent mode).
 * 
 * @param level The current LibBPF log level.
 * @param format The message format.
 * @param args Format arguments for the message.
 * 
 * @return void
 */
static int libbpf_silent(enum libbpf_print_level level, const char *format, va_list args)
{
    return 0;
}

/**
 * Sets custom LibBPF log mode.
 * 
 * @param silent If 1, disables LibBPF logging entirely.
 * 
 * @return void
 */
void set_libbpf_log_mode(int silent)
{
    if (silent)
    {
        libbpf_set_print(libbpf_silent);
    }
}

/**
 * Loads a BPF object file.
 * 
 * @param file_name The path to the BPF object file.
 * 
 * @return XDP program structure (pointer) or NULL.
 */
struct xdp_program *load_bpf_obj(const char *file_name)
{
    struct xdp_program *prog = xdp_program__open_file(file_name, "xdp_prog", NULL);

    if (prog == NULL)
    {
        // The main function handles this error.
        return NULL;
    }

    return prog;
}

/**
 * Retrieves BPF object from XDP program.
 * 
 * @param prog A pointer to the XDP program.
 * 
 * @return The BPF object.
 */
struct bpf_object* get_bpf_obj(struct xdp_program* prog)
{
    return xdp_program__bpf_obj(prog);
}

/**
 * Attempts to attach or detach (progfd = -1) a BPF/XDP program to an interface.
 * 
 * @param prog A pointer to the XDP program structure.
 * @param mode_used The mode being used.
 * @param ifidx The index to the interface to attach to.
 * @param detach If above 0, attempts to detach XDP program.
 * @param force_skb If set, forces the XDP program to run in SKB mode.
 * @param force_offload If set, forces the XDP program to run in offload mode.
 * 
 * @return 0 on success and 1 on error.
 */
int attach_xdp(struct xdp_program *prog, char** mode, int ifidx, int detach, int force_skb, int force_offload)
{
    int err;

    // Before attaching, set chaining options.
    xdp_program__set_run_prio(prog, XDP_MULTIPROG_PRIORITY);
    xdp_program__set_chain_call_enabled(prog, XDP_MULTIPROG_ACTION, XDP_MULTIPROG_ENABLED);

    u32 attach_mode = XDP_MODE_NATIVE;

    *mode = "DRV/native";

    if (force_offload)
    {
        *mode = "HW/offload";

        attach_mode = XDP_MODE_HW;
    }
    else if (force_skb)
    {
        *mode = "SKB/generic";
        
        attach_mode = XDP_MODE_SKB;
    }

    int exit = 0;

    while (!exit)
    {
        // Try loading program with current mode.
        int err;

        if (detach)
        {
            err = xdp_program__detach(prog, ifidx, attach_mode, 0);
        }
        else
        {
            err = xdp_program__attach(prog, ifidx, attach_mode, 0);
        }

        if (err)
        {
            // Decrease mode.
            switch (attach_mode)
            {
                case XDP_MODE_HW:
                    attach_mode = XDP_MODE_NATIVE;
                    *mode = "DRV/native";

                    break;

                case XDP_MODE_NATIVE:
                    attach_mode = XDP_MODE_SKB;
                    *mode = "SKB/generic";

                    break;

                case XDP_MODE_SKB:
                    // Exit loop.
                    exit = 1;

                    *mode = NULL;
                    
                    break;
            }

            // Retry.
            continue;
        }
        
        // Success, so break current loop.
        break;
    }

    // If exit is set to 1 or smode is NULL, it indicates full failure.
    if (exit || *mode == NULL)
    {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

/**
 * Deletes a forward rule from the BPF map.
 * 
 * @param map_fwd_rules The forward rules BPF map FD.
 * @param rule The forward rule to delete.
 * 
 * @return 0 on success, 2 on if bind IP or protocol isn't specified, or the error value of bpf_map_delete_elem().
 */
int delete_fwd_rule(int map_fwd_rules, fwd_rule_cfg_t* rule)
{
    int ret;

    if (!rule->bind_ip || !rule->protocol)
    {
        return 2;
    }

    // Construct key.
    struct in_addr bind_ip_addr;

    if ((ret = inet_pton(AF_INET, rule->bind_ip, &bind_ip_addr)) != 1)
    {
        return ret;
    }

    u16 bind_port = htons(rule->bind_port);

    char protocol_str[64];
    strncpy(protocol_str, rule->protocol, sizeof(protocol_str) - 1);
    protocol_str[sizeof(protocol_str) - 1] = '\0';

    int protocol = get_protocol_id_by_str(protocol_str);

    if (bind_port < 0)
    {
        return 1;
    }

    fwd_rule_key_t key = {0};
    key.ip = bind_ip_addr.s_addr;
    key.port = bind_port;
    key.protocol = protocol;

    return bpf_map_delete_elem(map_fwd_rules, &key);
}

/**
 * Deletes all forward rules from the BPF map.
 * 
 * @param map_fwd_rules The rules BPF map FD.
 * @param cfg A pointer to the config structure.
 * 
 * @return void
 */
void delete_fwd_rules(int map_fwd_rules, config__t *cfg)
{
    for (int i = 0; i < MAX_FWD_RULES; i++)
    {
        fwd_rule_cfg_t* rule = &cfg->rules[i];

        delete_fwd_rule(map_fwd_rules, rule);
    }
}

/**
 * Updates a forward rule in the BPF map.
 * 
 * @param map_fwd_rules The rules BPF map FD.
 * @param rule A pointer to the config rule.
 * 
 * @return 0 on success, 2 on bind IP, protocol, or destination IP isn't specified, or error value of bpf_map_update_elem().
 */
int update_fwd_rule(int map_fwd_rules, fwd_rule_cfg_t* rule)
{
    int ret;

    if (!rule->bind_ip || !rule->protocol || !rule->dst_ip)
    {
        return 2;
    }

    // Construct key.
    struct in_addr bind_ip_addr;

    if ((ret = inet_pton(AF_INET, rule->bind_ip, &bind_ip_addr)) != 1)
    {
        return ret;
    }

    u16 bind_port = htons(rule->bind_port);

    char protocol_str[64];
    strncpy(protocol_str, rule->protocol, sizeof(protocol_str) - 1);
    protocol_str[sizeof(protocol_str) - 1] = '\0';

    int protocol = get_protocol_id_by_str(protocol_str);

    if (bind_port < 0)
    {
        return 1;
    }

    fwd_rule_key_t key = {0};
    key.ip = bind_ip_addr.s_addr;
    key.port = bind_port;
    key.protocol = protocol;

    // Construct value.
    struct in_addr dst_ip_addr;

    if ((ret = inet_pton(AF_INET, rule->dst_ip, &dst_ip_addr)) != 1)
    {
        return ret;
    }

    u32 dst_port = htons(rule->dst_port);

    fwd_rule_val_t val = {0};
    val.log = rule->log;

    val.dst_ip = dst_ip_addr.s_addr;
    val.dst_port = dst_port;

    return bpf_map_update_elem(map_fwd_rules, &key, &val, BPF_ANY);
}

/**
 * Updates the forward rules in the BPF map.
 * 
 * @param map_fwd_rules The forward rule's BPF map FD.
 * @param cfg A pointer to the config structure.
 * 
 * @return Void
 */
void update_fwd_rules(int map_fwd_rules, config__t *cfg)
{
    int ret;

    // Add a rule to the rules maps.
    for (int i = 0; i < cfg->rules_cnt; i++)
    {
        fwd_rule_cfg_t* rule = &cfg->rules[i];

        // Only insert set and enabled rules.
        if (!rule->set || !rule->enabled)
        {
            continue;
        }

        // Attempt to update rule.
        if ((ret = update_fwd_rule(map_fwd_rules, rule)) != 0)
        {
            if (ret != 2)
            {
                log_msg(cfg, 1, 0, "[WARNING] Failed to update rule '%s:%d' (%s) due to BPF update error (%d)...", rule->bind_ip, rule->bind_port, rule->protocol, ret);
            }
            else
            {
                log_msg(cfg, 1, 0, "[WARNING] Failed to update rule at index %d. Bind IP, protocol, or destination IP is not specified...", i + 1);
            }

            continue;
        }
    }
}

/**
 * Pins a BPF map to the file system.
 * 
 * @param obj A pointer to the BPF object.
 * @param pin_dir The pin directory.
 * @param map_name The map name.
 * 
 * @return 0 on success or value of bpf_map__pin() on error.
 */
int pin_map(struct bpf_object* obj, const char* pin_dir, const char* map_name)
{
    struct bpf_map* map = bpf_object__find_map_by_name(obj, map_name);

    if (!map)
    {
        return -1;
    }

    char full_path[255];
    snprintf(full_path, sizeof(full_path), "%s/%s", XDP_MAP_PIN_DIR, map_name);

    return bpf_map__pin(map, full_path);
}

/**
 * Unpins a BPF map from the file system.
 * 
 * @param obj A pointer to the BPF object.
 * @param pin_dir The pin directory.
 * @param map_name The map name.
 * 
 * @return
 */
int unpin_map(struct bpf_object* obj, const char* pin_dir, const char* map_name)
{
    struct bpf_map* map = bpf_object__find_map_by_name(obj, map_name);

    if (!map)
    {
        return 1;
    }

    char full_path[255];
    snprintf(full_path, sizeof(full_path), "%s/%s", XDP_MAP_PIN_DIR, map_name);

    return bpf_map__unpin(map, full_path);
}

/**
 * Retrieves a map FD on the file system (pinned).
 * 
 * @param pin_dir The pin directory.
 * @param map_name The map name.
 * 
 * @return The map FD or -1 on error.
 */
int get_map_pin_fd(const char* pin_dir, const char* map_name)
{
    char full_path[255];
    snprintf(full_path, sizeof(full_path), "%s/%s", pin_dir, map_name);

    return bpf_obj_get(full_path);
}