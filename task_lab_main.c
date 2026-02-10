/****************************************************************************
 * apps/examples/task_lab/task_lab_main.c
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.
 *
 * A demonstration of NuttX task control mechanisms:
 *   - Task creation (task_create) and deletion (task_delete)
 *   - Cooperative delay (sleep/usleep) vs suspension (pause + signals)
 *   - Preemption locking (sched_lock/sched_unlock)
 *   - Run-to-Completion vs Endless Loop task patterns
 *   - Parent-child synchronization (waitpid)
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sched.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <nuttx/leds/userled.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define TASK_STACKSIZE    2048
#define TASK_PRIORITY     100
#define LED_BLINK_US      500000   /* 500 ms in microseconds */

/****************************************************************************
 * Private Data — Shared State for Task Control
 ****************************************************************************/

/* The control task sets these flags; the LED task reads them each iteration.
 * 'volatile' ensures the compiler always reads from memory, not a register.
 */

static volatile bool g_led_suspended = false;  /* true = LED task should suspend */
static volatile bool g_led_running   = true;   /* false = LED task should exit   */

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: sigusr1_handler
 *
 * Description:
 *   Empty signal handler for SIGUSR1. Its only purpose is to make pause()
 *   return so the LED task can check the g_led_suspended flag again.
 *   Without a handler, SIGUSR1's default action would terminate the task.
 *
 ****************************************************************************/

static void sigusr1_handler(int signo)
{
  /* Intentionally empty — just interrupt pause() */
}

/****************************************************************************
 * Name: led_task — ENDLESS LOOP pattern
 *
 * Description:
 *   Blinks all 4 LEDs via /dev/userleds. Runs indefinitely until
 *   g_led_running is set to false or the task is deleted.
 *
 *   Demonstrates:
 *     - Endless loop task structure
 *     - usleep() for cooperative delay (task enters WAITING state)
 *     - Suspend/resume via shared flag + pause() + signal
 *     - Opening device files and using ioctl()
 *
 ****************************************************************************/

/* [STUDENT: IMPLEMENT THIS FUNCTION]
 *
 * Requirements:
 *   1. Install a SIGUSR1 handler using sigaction() so pause() can return
 *   2. Open "/dev/userleds" with open()
 *   3. Loop while g_led_running is true:
 *      a. If g_led_suspended is true:
 *         - Print a "SUSPENDED" message
 *         - Call pause() to block until a signal arrives
 *         - When pause() returns, print "RESUMED" and continue the loop
 *      b. Otherwise:
 *         - Toggle LEDs ON/OFF using ioctl(fd, ULEDIOC_SETALL, ...)
 *         - Print the current state (ON/OFF) with iteration number
 *         - Call usleep(LED_BLINK_US) to delay — this YIELDS the CPU
 *   4. After the loop, turn off all LEDs, close fd, print exit message
 */

static int led_task(int argc, char *argv[])
{
  struct sigaction act;
  int fd;
  int iteration = 0;
  bool leds_on = false;
  pid_t mypid = getpid();

  printf("[LED Task PID %d] Starting (Endless Loop pattern)\n", mypid);

  /* Install SIGUSR1 handler so pause() is interrupted without killing us */

  memset(&act, 0, sizeof(struct sigaction));
  act.sa_handler = sigusr1_handler;
  sigemptyset(&act.sa_mask);
  sigaction(SIGUSR1, &act, NULL);

  /* Open the user LED device */

  fd = open("/dev/userleds", O_WRONLY);
  if (fd < 0)
    {
      printf("[LED Task PID %d] ERROR: Failed to open /dev/userleds: %d\n",
             mypid, errno);
      return EXIT_FAILURE;
    }

  /* ---- Endless Loop ---- */

  while (g_led_running)
    {
      /* Check if we have been suspended by the control task */

      if (g_led_suspended)
        {
          printf("[LED Task PID %d] SUSPENDED — LEDs frozen, "
                 "waiting for resume signal...\n", mypid);

          /* pause() blocks this task until ANY signal is delivered.
           * The task enters the BLOCKED state — the scheduler runs
           * other tasks while we wait.
           */

          pause();

          printf("[LED Task PID %d] RESUMED — continuing blink\n", mypid);
          continue;
        }

      /* Normal operation: toggle LEDs */

      iteration++;
      leds_on = !leds_on;

      ioctl(fd, ULEDIOC_SETALL,
            (unsigned long)(leds_on ? 0x0f : 0x00));
      printf("[LED Task PID %d] Iter %d — LEDs %s\n",
             mypid, iteration, leds_on ? "ON" : "OFF");

      /* usleep() puts this task in WAITING state for the given duration.
       * The CPU is FREE for other tasks during this time.
       * Compare this with the busy-wait in Phase 4!
       */

      usleep(LED_BLINK_US);
    }

  /* Cleanup: turn off all LEDs before exiting */

  ioctl(fd, ULEDIOC_SETALL, (unsigned long)0x00);
  close(fd);
  printf("[LED Task PID %d] Terminated cleanly\n", mypid);
  return EXIT_SUCCESS;
}

/****************************************************************************
 * Name: counter_task — RUN-TO-COMPLETION pattern
 *
 * Description:
 *   Counts from 1 to 5, then exits. Does NOT loop forever.
 *
 *   Demonstrates:
 *     - Run-to-completion task structure (finite work, natural exit)
 *     - sleep() for cooperative delay
 *     - Parent collects exit status via waitpid()
 *
 ****************************************************************************/

/* [STUDENT: IMPLEMENT THIS FUNCTION]
 *
 * Requirements:
 *   1. Print a startup message with PID and pattern name
 *   2. Loop from 1 to 5:
 *      a. Print the current count
 *      b. sleep(1)
 *   3. Print a completion message
 *   4. Return EXIT_SUCCESS (the parent will see this via waitpid)
 */

static int counter_task(int argc, char *argv[])
{
  int i;
  pid_t mypid = getpid();

  printf("[Counter PID %d] Starting (Run-to-Completion pattern)\n", mypid);

  for (i = 1; i <= 5; i++)
    {
      printf("[Counter PID %d] Count: %d / 5\n", mypid, i);
      sleep(1);
    }

  printf("[Counter PID %d] Work complete — exiting naturally\n", mypid);
  return EXIT_SUCCESS;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * task_lab_main — CONTROL TASK
 *
 * Description:
 *   The main entry point acts as a "control task" that orchestrates
 *   five phases demonstrating different task control mechanisms.
 *
 ****************************************************************************/

/* [STUDENT: IMPLEMENT THE PHASE 2–5 SECTIONS]
 *
 * Phase 1 (provided): Create tasks, observe concurrency
 * Phase 2: Suspend the LED task using the shared flag
 * Phase 3: Resume the LED task using kill(pid, SIGUSR1)
 * Phase 4: Demonstrate sched_lock() with a busy-wait
 * Phase 5: Delete the LED task with task_delete()
 *
 * Hints:
 *   - task_create() returns PID on success, -1 on failure
 *   - waitpid(pid, &status, 0) blocks until the task exits
 *   - kill(pid, SIGUSR1) sends SIGUSR1 to wake a paused task
 *   - sched_lock() prevents preemption; sched_unlock() restores it
 *   - task_delete(pid) forcefully removes a running task
 */

int main(int argc, FAR char *argv[])
{
  pid_t led_pid;
  pid_t counter_pid;
  int status;

  printf("\n============================================\n");
  printf("  NuttX Task Control Lab (PID %d)\n", getpid());
  printf("============================================\n\n");

  /* Reset shared state (important if the app is run more than once) */

  g_led_suspended = false;
  g_led_running   = true;

  /* ===== PHASE 1: Task Creation & Cooperative Delay =============== */

  printf("--- Phase 1: Task Creation & Cooperative Delay ---\n\n");
  printf("[Control] Creating LED task (Endless Loop)...\n");

  led_pid = task_create("led_task",
                        TASK_PRIORITY,
                        TASK_STACKSIZE,
                        led_task,
                        NULL);
  if (led_pid < 0)
    {
      printf("[Control] ERROR: Failed to create LED task: %d\n", errno);
      return EXIT_FAILURE;
    }

  printf("[Control] LED task created with PID %d\n", led_pid);
  printf("[Control] Creating counter task (Run-to-Completion)...\n");

  counter_pid = task_create("counter",
                            TASK_PRIORITY,
                            TASK_STACKSIZE,
                            counter_task,
                            NULL);
  if (counter_pid < 0)
    {
      printf("[Control] ERROR: Failed to create counter task: %d\n", errno);
      return EXIT_FAILURE;
    }

  printf("[Control] Counter task created with PID %d\n\n", counter_pid);
  printf("[Control] Both tasks running concurrently. "
         "Observe interleaved output for 6 seconds...\n\n");

  /* Let both tasks run — usleep/sleep in each task yields the CPU,
   * allowing all three tasks (control, LED, counter) to share time.
   */

  sleep(6);

  /* The counter task should have finished by now (5 iterations × 1s) */

  waitpid(counter_pid, &status, 0);
  printf("\n[Control] Counter task exited with status %d "
         "(Run-to-Completion done)\n\n", WEXITSTATUS(status));

  /* ===== PHASE 2: Task Suspension ================================ */

  printf("--- Phase 2: Suspending the LED Task ---\n\n");
  printf("[Control] Setting g_led_suspended = true\n");
  printf("[Control] The LED task will see this flag and call pause().\n");
  printf("[Control] LEDs will FREEZE in their current state.\n\n");

  g_led_suspended = true;

  /* Give the LED task a moment to notice the flag and enter pause() */

  sleep(1);

  printf("[Control] LED task is now suspended (BLOCKED on pause()).\n");
  printf("[Control] Waiting 3 seconds — notice NO LED output below:\n\n");

  sleep(3);

  /* ===== PHASE 3: Task Resumption ================================ */

  printf("--- Phase 3: Resuming the LED Task ---\n\n");
  printf("[Control] Clearing suspend flag and sending SIGUSR1...\n");

  g_led_suspended = false;
  kill(led_pid, SIGUSR1);   /* Interrupt pause() in the LED task */

  printf("[Control] LED task should resume blinking. "
         "Observe output for 4 seconds...\n\n");

  sleep(4);

  /* ===== PHASE 4: Preemption Lock ================================ */

  printf("\n--- Phase 4: Preemption Lock Demo (sched_lock) ---\n\n");
  printf("[Control] Calling sched_lock() — the control task CANNOT be\n");
  printf("          preempted. The LED task will be STARVED.\n\n");

  sched_lock();

  /* While preemption is locked, we do a busy-wait loop.
   * Unlike sleep()/usleep(), a busy-wait does NOT yield the CPU.
   * The LED task (and all other tasks) cannot run during this time.
   *
   * THIS IS DANGEROUS in a real system — it starves all other tasks!
   */

  volatile unsigned long busy;
  int tick;

  for (tick = 1; tick <= 4; tick++)
    {
      printf("[Control] Preemption LOCKED — tick %d/4 "
             "(LED task starved!)\n", tick);

      /* Busy-wait loop — does NOT yield CPU */

      for (busy = 0; busy < 3000000UL; busy++);
    }

  sched_unlock();

  printf("\n[Control] sched_unlock() called — normal scheduling resumes.\n");
  printf("[Control] LED task can run again. "
         "Observe for 3 seconds...\n\n");

  sleep(3);

  /* ===== PHASE 5: Task Deletion ================================== */

  printf("--- Phase 5: Deleting the LED Task ---\n\n");
  printf("[Control] Calling task_delete(%d)...\n", led_pid);
  printf("[Control] The LED task will be forcefully removed.\n");
  printf("[Control] LEDs will freeze in whatever state they were in.\n\n");

  g_led_running = false;    /* Belt-and-suspenders: clear the flag too */
  task_delete(led_pid);

  printf("[Control] LED task deleted. No more LED output.\n\n");

  /* ===== SUMMARY ================================================= */

  printf("============================================\n");
  printf("  Task Control Lab Complete!\n");
  printf("============================================\n");
  printf("  Demonstrated:\n");
  printf("    - task_create()     : Spawn new tasks\n");
  printf("    - sleep()/usleep()  : Cooperative delay (yields CPU)\n");
  printf("    - pause()/kill()    : Suspend and resume a task\n");
  printf("    - sched_lock()      : Lock preemption (starves others)\n");
  printf("    - sched_unlock()    : Restore normal scheduling\n");
  printf("    - task_delete()     : Forcefully remove a task\n");
  printf("    - waitpid()         : Wait for task completion\n");
  printf("    - Endless Loop vs Run-to-Completion patterns\n");
  printf("============================================\n");

  return EXIT_SUCCESS;
}
