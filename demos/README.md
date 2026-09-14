# Penguin Shell demo scripts

These `.psh` files are meant to be shown off live with the `-f` flag, which
feeds a script into the shell the same way `~/.penrc` is loaded at startup:
each line runs with history recording turned off. Once the script finishes,
this shell instance exits -- `-f` is for running a script, not seeding an
interactive session, so start a plain `penguin` afterward to keep exploring.

```
penguin -f demos/beginner.psh
penguin -f demos/intermediate.psh
penguin -f demos/engineer.psh
penguin -f demos/jobs.psh
```

Run them from the repository root so relative paths (like `demos` or `src`)
resolve correctly.

- **beginner.psh** -- for someone who has never used a terminal. Narrates
  every step in plain language: what a shell is, `pwd`/`ls`/`cd`, writing to
  a file, a simple loop, cleaning up after itself.
- **intermediate.psh** -- for someone who already knows a terminal but not
  this shell. Covers variables (`xpt`), aliases, exit status, `&&`/`||`,
  pipes and redirection, `if`/`for`/`until`.
- **engineer.psh** -- for engineers. Multi-stage pipelines, backgrounding
  with `&`, the git-aware prompt, and notes on where this shell currently
  differs from bash/zsh (no arithmetic expansion, no fd-numbered redirects
  like `2>`, bare `[ x = y ]` string equality isn't reliable yet).
- **jobs.psh** -- a flashier follow-up: runs 10 real `python3` jobs
  (`job_worker.py`), a `for` loop branching on each one's exit status, and a
  report built purely from `>>` redirection and `grep -c` (no arithmetic
  needed). Good for showing the shell driving actual work instead of just
  narrating its own syntax.
