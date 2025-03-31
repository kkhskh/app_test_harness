#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include "../recovery_evaluator/recovery_evaluator.h"

/* Define driver classes */
#define DRIVER_CLASS_SOUND   "snd"
#define DRIVER_CLASS_NETWORK "e1000"
#define DRIVER_CLASS_IDE     "ide"

/* Define test applications */
struct app_definition {
    char name[64];
    char driver_class[32];
    int num_trials;
    int automatic_recovery;
    int manual_recovery;
    int failed_recovery;
    bool is_running;
};

static struct app_definition test_apps[] = {
    { "mp3_player", DRIVER_CLASS_SOUND, 0, 0, 0, 0, false },
    { "audio_recorder", DRIVER_CLASS_SOUND, 0, 0, 0, 0, false },
    { "network_file_transfer", DRIVER_CLASS_NETWORK, 0, 0, 0, 0, false },
    { "network_analyzer", DRIVER_CLASS_NETWORK, 0, 0, 0, 0, false },
    { "compiler", DRIVER_CLASS_IDE, 0, 0, 0, 0, false },
    { "database", DRIVER_CLASS_IDE, 0, 0, 0, 0, false }
};

#define NUM_TEST_APPS (sizeof(test_apps) / sizeof(test_apps[0]))

/* Thread to simulate application behavior */
static struct task_struct *app_thread = NULL;

/* Thread function that simulates application requests to driver */
static int app_thread_fn(void *data)
{
    int app_idx = *(int *)data;
    int i, trial = 0;
    char test_name[128];
    
    test_apps[app_idx].is_running = true;
    
    while (!kthread_should_stop() && trial < 400) { // Run 400 trials as in paper
        /* Start a new test trial */
        snprintf(test_name, sizeof(test_name), 
                 "%s_trial_%d", test_apps[app_idx].name, trial);
        
        start_test(test_name, test_apps[app_idx].driver_class);
        
        /* Simulate application interacting with driver */
        for (i = 0; i < 10 && !kthread_should_stop(); i++) {
            /* Each app would make different calls to the driver */
            /* For now, just sleep to simulate activity */
            msleep(100);
        }
        
        if (kthread_should_stop())
            break;
            
        /* Trigger a fault in the driver */
        /* You would normally call into fault_injection.c */
        add_event(NULL, PHASE_FAILURE_DETECTED, "Injected fault in %s", 
                  test_apps[app_idx].driver_class);
                  
        /* Wait for recovery to happen */
        msleep(2000);
        
        /* Check if application still works after recovery */
        if (1) { /* REPLACE with actual check */
            /* This should be a real check whether the app still functions */
            test_apps[app_idx].automatic_recovery++;
            add_event(NULL, PHASE_RECOVERY_COMPLETE, "Automatic recovery successful");
            end_test(true);
        } else {
            /* Try manual recovery */
            add_event(NULL, PHASE_DRIVER_RESTARTING, "Attempting manual recovery");
            msleep(1000);
            
            if (1) { /* REPLACE with actual check */
                test_apps[app_idx].manual_recovery++;
                add_event(NULL, PHASE_RECOVERY_COMPLETE, "Manual recovery successful");
                end_test(true);
            } else {
                test_apps[app_idx].failed_recovery++;
                add_event(NULL, PHASE_RECOVERY_FAILED, "Recovery failed");
                end_test(false);
            }
        }
        
        test_apps[app_idx].num_trials++;
        trial++;
        
        /* Wait between trials */
        msleep(1000);
    }
    
    test_apps[app_idx].is_running = false;
    return 0;
}

/* Proc file operations */
static int harness_proc_show(struct seq_file *m, void *v)
{
    int i;
    
    seq_printf(m, "App Test Harness Status:\n\n");
    
    for (i = 0; i < NUM_TEST_APPS; i++) {
        seq_printf(m, "App %d: %s (Driver: %s)\n", 
                   i, test_apps[i].name, test_apps[i].driver_class);
        seq_printf(m, "  Running: %s\n", 
                   test_apps[i].is_running ? "Yes" : "No");
        seq_printf(m, "  Trials: %d\n", test_apps[i].num_trials);
        seq_printf(m, "  Automatic Recovery: %d (%.1f%%)\n", 
                   test_apps[i].automatic_recovery,
                   test_apps[i].num_trials > 0 ? 
                      (float)test_apps[i].automatic_recovery * 100 / test_apps[i].num_trials : 0);
        seq_printf(m, "  Manual Recovery: %d (%.1f%%)\n", 
                   test_apps[i].manual_recovery,
                   test_apps[i].num_trials > 0 ? 
                      (float)test_apps[i].manual_recovery * 100 / test_apps[i].num_trials : 0);
        seq_printf(m, "  Failed Recovery: %d (%.1f%%)\n\n", 
                   test_apps[i].failed_recovery,
                   test_apps[i].num_trials > 0 ? 
                      (float)test_apps[i].failed_recovery * 100 / test_apps[i].num_trials : 0);
    }
    
    return 0;
}

static int harness_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, harness_proc_show, NULL);
}

static ssize_t harness_proc_write(struct file *file, const char __user *buffer,
                                size_t count, loff_t *ppos)
{
    char cmd[64];
    int app_idx;
    size_t cmd_size = min(count, sizeof(cmd) - 1);
    static int current_app = 0;  /* For passing to thread */
    
    if (copy_from_user(cmd, buffer, cmd_size))
        return -EFAULT;
    
    cmd[cmd_size] = '\0';
    
    /* Process command */
    if (sscanf(cmd, "start %d", &app_idx) == 1 && 
        app_idx >= 0 && app_idx < NUM_TEST_APPS) {
        
        if (test_apps[app_idx].is_running) {
            printk(KERN_WARNING "App %d is already running\n", app_idx);
            return count;
        }
        
        current_app = app_idx;
        app_thread = kthread_run(app_thread_fn, &current_app, 
                                "app_test_%s", test_apps[app_idx].name);
        if (IS_ERR(app_thread)) {
            printk(KERN_ERR "Failed to start app test thread\n");
            return PTR_ERR(app_thread);
        }
        
    } else if (strncmp(cmd, "stop", 4) == 0) {
        if (app_thread) {
            kthread_stop(app_thread);
            app_thread = NULL;
        }
    } else if (strncmp(cmd, "reset", 5) == 0) {
        /* Reset all test statistics */
        for (app_idx = 0; app_idx < NUM_TEST_APPS; app_idx++) {
            test_apps[app_idx].num_trials = 0;
            test_apps[app_idx].automatic_recovery = 0;
            test_apps[app_idx].manual_recovery = 0;
            test_apps[app_idx].failed_recovery = 0;
        }
    }
    
    return count;
}

static const struct proc_ops harness_proc_ops = {
    .proc_open = harness_proc_open,
    .proc_read = seq_read,
    .proc_write = harness_proc_write,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};

static struct proc_dir_entry *harness_proc_entry;

static int __init app_test_harness_init(void)
{
    /* Create proc entry */
    harness_proc_entry = proc_create("app_test_harness", 0644, NULL, &harness_proc_ops);
    if (!harness_proc_entry)
        return -ENOMEM;
    
    printk(KERN_INFO "App Test Harness loaded\n");
    printk(KERN_INFO "Use /proc/app_test_harness to control tests\n");
    
    return 0;
}

static void __exit app_test_harness_exit(void)
{
    if (app_thread)
        kthread_stop(app_thread);
    
    if (harness_proc_entry)
        proc_remove(harness_proc_entry);
    
    printk(KERN_INFO "App Test Harness unloaded\n");
}

module_init(app_test_harness_init);
module_exit(app_test_harness_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Shadow Driver Replication Project");
MODULE_DESCRIPTION("Test harness for shadow driver fault injection");