#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#include <signal.h>

#include <sys/resource.h>
#include <sys/sysinfo.h>
#include <sys/stat.h>

#include <net/if.h>

#include <loader/utils/cli.h>
#include <loader/utils/config.h>
#include <loader/utils/xdp.h>
#include <loader/utils/logging.h>
#include <loader/utils/stats.h>
#include <loader/utils/helpers.h>

int cont = 1;
int doing_stats = 0;

/**
 * Unpins required BPF maps from file system.
 * 
 * @param cfg A pointer to the config structure.
 * @param obj A pointer to the BPF object.
 * @param ignore_errors Whether to ignore errors.
 */
static void unpin_needed_maps(config__t* cfg, struct bpf_object* obj, int ignore_errors)
{
    int ret;

    // Unpin forward rules map.
    if ((ret = unpin_map(obj, XDP_MAP_PIN_DIR, "map_fwd_rules")) != 0)
    {
        if (!ignore_errors)
        {
            log_msg(cfg, 1, 0, "[WARNING] Failed to un-pin BPF map 'map_block' from file system (%d).", ret);
        }
    }
}

int main(int argc, char *argv[])
{
    int ret;

    // Parse the command line.
    cli_t cli = {0};
    cli.cfg_file = CONFIG_DEFAULT_PATH;
    cli.verbose = -1;
    cli.pin_maps = -1;
    cli.update_time = -1;
    cli.no_stats = -1;
    cli.stats_per_second = -1;
    cli.stdout_update_time = -1;

    parse_cli(&cli, argc, argv);

    // Check for help.
    if (cli.help)
    {
        print_help_menu();

        return EXIT_SUCCESS;
    }

    // Initialize config.
    config__t cfg = {0};

    set_cfg_defaults(&cfg);

    // Create overrides for config and set arguments from CLI.
    config_overrides_t cfg_overrides = {0};
    cfg_overrides.verbose = cli.verbose;
    cfg_overrides.log_file = cli.log_file;
    cfg_overrides.interface = cli.interface;
    cfg_overrides.pin_maps = cli.pin_maps;
    cfg_overrides.update_time = cli.update_time;
    cfg_overrides.no_stats = cli.no_stats;
    cfg_overrides.stats_per_second = cli.stats_per_second;
    cfg_overrides.stdout_update_time = cli.stdout_update_time;

    // Load config.
    if ((ret = load_config(&cfg, cli.cfg_file, &cfg_overrides)) != 0)
    {
        fprintf(stderr, "[ERROR] Failed to load config from file system (%s)(%d).\n", cli.cfg_file, ret);

        return EXIT_FAILURE;
    }

    // Check for list option.
    if (cli.list)
    {
        print_config(&cfg);

        return EXIT_SUCCESS;
    }

    // Print tool info.
    if (cfg.verbose > 0)
    {
        print_tool_info();
    }

    // Check interface.
    if (cfg.interface == NULL)
    {
        log_msg(&cfg, 0, 1, "[ERROR] No interface specified in config or CLI override.");

        return EXIT_FAILURE;
    }

    log_msg(&cfg, 2, 0, "Raising RLimit...");

    // Raise RLimit.
    struct rlimit rl = { RLIM_INFINITY, RLIM_INFINITY };

    if (setrlimit(RLIMIT_MEMLOCK, &rl)) 
    {
        log_msg(&cfg, 0, 1, "[ERROR] Failed to raise rlimit. Please make sure this program is ran as root!\n");

        return EXIT_FAILURE;
    }

    log_msg(&cfg, 2, 0, "Retrieving interface index for '%s'...", cfg.interface);

    // Get interface index.
    int ifidx = if_nametoindex(cfg.interface);

    if (ifidx < 0)
    {
        log_msg(&cfg, 0, 1, "[ERROR] Failed to retrieve index of network interface '%s'.\n", cfg.interface);

        return EXIT_FAILURE;
    }

    log_msg(&cfg, 2, 0, "Loading XDP/BPF program at '%s'...", XDP_OBJ_PATH);

    // Determine custom LibBPF log level.
    int silent = 1;

    if (cfg.verbose > 4)
    {
        silent = 0;
    }

    set_libbpf_log_mode(silent);

    // Load BPF object.
    struct xdp_program *prog = load_bpf_obj(XDP_OBJ_PATH);

    if (prog == NULL)
    {
        log_msg(&cfg, 0, 1, "[ERROR] Failed to load eBPF object file. Object path => %s.\n", XDP_OBJ_PATH);

        return EXIT_FAILURE;
    }

    log_msg(&cfg, 2, 0, "Attaching XDP program to interface '%s'...", cfg.interface);
    
    // Attach XDP program.
    char *mode_used = NULL;

    if ((ret = attach_xdp(prog, &mode_used, ifidx, 0, cli.skb, cli.offload)) != 0)
    {
        log_msg(&cfg, 0, 1, "[ERROR] Failed to attach XDP program to interface '%s' using available modes (%d).\n", cfg.interface, ret);

        return EXIT_FAILURE;
    }

    if (mode_used != NULL)
    {
        log_msg(&cfg, 1, 0, "Attached XDP program using mode '%s'...", mode_used);
    }

    log_msg(&cfg, 2, 0, "Retrieving BPF map FDs...");

    // Retrieve BPF maps.
    int map_stats = get_map_fd(prog, "map_stats");

    if (map_stats < 0)
    {
        log_msg(&cfg, 0, 1, "[ERROR] Failed to find 'map_stats' BPF map.\n");

        return EXIT_FAILURE;
    }

    log_msg(&cfg, 3, 0, "map_stats FD => %d.", map_stats);

    int map_fwd_rules = get_map_fd(prog, "map_fwd_rules");

    // Check for valid maps.
    if (map_fwd_rules < 0)
    {
        log_msg(&cfg, 0, 1, "[ERROR] Failed to find 'map_fwd_rules' BPF map.\n");

        return EXIT_FAILURE;
    }

    log_msg(&cfg, 3, 0, "map_fwd_rules FD => %d.", map_fwd_rules);

#ifdef ENABLE_RULE_LOGGING
    int map_fwd_rules_log = get_map_fd(prog, "map_fwd_rules_log");

    struct ring_buffer* rb = NULL;

    if (map_fwd_rules_log < 0)
    {
        log_msg(&cfg, 1, 0, "[WARNING] Failed to find 'map_fwd_rules_log' BPF map. Rule logging will be disabled...");
    }
    else
    {
        log_msg(&cfg, 3, 0, "map_fwd_rules_log FD => %d.", map_fwd_rules_log);

        rb = ring_buffer__new(map_fwd_rules_log, handle_fwd_rules_rb_event, &cfg, NULL);
    }
#endif

    // Pin BPF maps to file system if we need to.
    if (cfg.pin_maps)
    {
        log_msg(&cfg, 2, 0, "Pinning BPF maps...");

        struct bpf_object* obj = get_bpf_obj(prog);

        // There are times where the BPF maps from the last run weren't cleaned up properly.
        // So it's best to attempt to unpin the maps before pinning while ignoring errors.
        unpin_needed_maps(&cfg, obj, 1);

        // Pin the block maps.
        if ((ret = pin_map(obj, XDP_MAP_PIN_DIR, "map_fwd_rules")) != 0)
        {
            log_msg(&cfg, 1, 0, "[WARNING] Failed to pin 'map_fwd_rules' to file system (%d)...", ret);
        }
        else
        {
            log_msg(&cfg, 3, 0, "BPF map 'map_fwd_rules' pinned to '%s/map_fwd_rules'.", XDP_MAP_PIN_DIR);
        }
    }

    log_msg(&cfg, 2, 0, "Updating rules...");

    // Update rules.
    update_fwd_rules(map_fwd_rules, &cfg);

    // Signal.
    signal(SIGINT, signal_hndl);
    signal(SIGTERM, signal_hndl);

    // Receive CPU count for stats map parsing.
    int cpus = get_nprocs_conf();

    log_msg(&cfg, 4, 0, "Retrieved %d CPUs on host.", cpus);

    unsigned int end_time = (cli.time > 0) ? time(NULL) + cli.time : 0;

    // Create last updated variables.
    time_t last_update_check = time(NULL);
    time_t last_config_check = time(NULL);

    unsigned int sleep_time = cfg.stdout_update_time * 1000;

    struct stat conf_stat;

    // Check if we're doing stats.
    if (!cfg.no_stats)
    {
        doing_stats = 1;
    }

    while (cont)
    {
        // Get current time.
        time_t cur_time = time(NULL);

        // Check if we should end the program.
        if (end_time > 0 && cur_time >= end_time)
        {
            break;
        }

        // Check for auto-update.
        if (cfg.update_time > 0 && (cur_time - last_update_check) > cfg.update_time)
        {
            // Check if config file have been modified
            if (stat(cli.cfg_file, &conf_stat) == 0 && conf_stat.st_mtime > last_config_check) {
                // Reload config.
                if ((ret = load_config(&cfg, cli.cfg_file, &cfg_overrides)) != 0)
                {
                    log_msg(&cfg, 1, 0, "[WARNING] Failed to load config after update check (%d)...\n", ret);
                }

                // Update rules.
                update_fwd_rules(map_fwd_rules, &cfg);

                // Update last check timer
                last_config_check = time(NULL);

                // Make sure we set doing stats if needed.
                if (!cfg.no_stats && !doing_stats)
                {
                    doing_stats = 1;
                }
            }

            // Update last updated variable.
            last_update_check = time(NULL);
        }

        // Calculate and display stats if enabled.
        if (!cfg.no_stats)
        {
            if (calc_stats(map_stats, cpus, cfg.stats_per_second))
            {
                log_msg(&cfg, 1, 0, "[WARNING] Failed to calculate packet stats. Stats map FD => %d...\n", map_stats);
            }
        }

#ifdef ENABLE_RULE_LOGGING
        poll_fwd_rules_rb(rb);
#endif

        usleep(sleep_time);
    }

    fprintf(stdout, "\n");

    log_msg(&cfg, 2, 0, "Cleaning up...");

#ifdef ENABLE_RULE_LOGGING
    if (rb)
    {
        ring_buffer__free(rb);
    }
#endif

    // Detach XDP program.
    if (attach_xdp(prog, &mode_used, ifidx, 1, cli.skb, cli.offload))
    {
        log_msg(&cfg, 0, 1, "[ERROR] Failed to detach XDP program from interface '%s'.\n", cfg.interface);

        return EXIT_FAILURE;
    }

    // Unpin maps from file system.
    if (cfg.pin_maps)
    {
        log_msg(&cfg, 2, 0, "Un-pinning BPF maps from file system...");

        struct bpf_object* obj = get_bpf_obj(prog);

        unpin_needed_maps(&cfg, obj, 0);
    }

    // Lastly, close the XDP program.
    xdp_program__close(prog);

    log_msg(&cfg, 1, 0, "Exiting.\n");

    // Exit program successfully.
    return EXIT_SUCCESS;
}