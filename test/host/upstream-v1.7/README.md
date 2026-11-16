# Upstream PSI Pro v1.7, for tests only

`PSIPro.ino`, `config.h` and `matrices.h` in this directory are Neil Hutchison's PSI Pro firmware, copied without
changing a byte from <https://github.com/nhutchison/PSIPro> at commit
`5e80597` ("Update to version 1.7", 2020-12-30). They are his work and his contributors'; see the top of
`PSIPro.ino` and the repository README for the credits.

Nothing here is built into the firmware. The files exist so that `test/host/diff_upstream.sh` can run Neil's
code and the fork's side by side on the same host mocks and compare what each puts on the panel. Do not edit
them: a difference from upstream would make that comparison meaningless. To move to a newer upstream release,
replace all three and re-run `bash test/host/run.sh`.
