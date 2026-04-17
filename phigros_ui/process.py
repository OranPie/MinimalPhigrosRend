"""Non-blocking process runner built on QProcess.

The renderer and build tabs both need to launch long-running subprocesses
without blocking the Qt event loop.  :class:`ProcessRunner` wraps ``QProcess``
with a minimal pub/sub API:

    runner = ProcessRunner()
    runner.output.connect(log_view.append)
    runner.finished.connect(lambda rc: ...)
    runner.start(["phigros_render", "chart.json"])

Multiple sequential commands are supported via :meth:`start_sequence`, used
by the build tab (configure + build + optional tests).
"""

from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import QObject, QProcess, Signal


class ProcessRunner(QObject):
    """Queue-based QProcess wrapper with merged stdout/stderr.

    Signals:
      * ``started(list[str])`` — fires whenever a new command begins.
      * ``output(str)``        — each chunk of merged stdout/stderr.
      * ``finished(int, str)`` — ``(exit_code, reason)``; reason is
        ``"ok"``, ``"error"``, ``"aborted"``, ``"crash"``.
      * ``sequence_finished(int)`` — fires after the whole queue ends.
    """

    started = Signal(list)
    output = Signal(str)
    finished = Signal(int, str)
    sequence_finished = Signal(int)

    def __init__(self, parent: QObject | None = None) -> None:
        super().__init__(parent)
        self._queue: list[list[str]] = []
        self._cwd: str | None = None
        self._process: QProcess | None = None
        self._aborted = False

    # ------------------------------------------------------------------ #
    # public                                                             #
    # ------------------------------------------------------------------ #

    def is_running(self) -> bool:
        return self._process is not None and self._process.state() != QProcess.NotRunning

    def start(self, command: list[str], cwd: str | Path | None = None) -> None:
        self.start_sequence([command], cwd=cwd)

    def start_sequence(self, commands: list[list[str]], cwd: str | Path | None = None) -> None:
        if self.is_running():
            raise RuntimeError("ProcessRunner is already running a command.")
        if not commands:
            self.sequence_finished.emit(0)
            return
        self._queue = [list(c) for c in commands]
        self._cwd = str(cwd) if cwd is not None else None
        self._aborted = False
        self._start_next()

    def abort(self) -> None:
        if not self.is_running():
            return
        self._aborted = True
        self._queue.clear()
        assert self._process is not None
        self._process.kill()

    # ------------------------------------------------------------------ #
    # internal                                                           #
    # ------------------------------------------------------------------ #

    def _start_next(self) -> None:
        if not self._queue:
            self.sequence_finished.emit(0)
            return
        command = self._queue.pop(0)
        process = QProcess(self)
        process.setProcessChannelMode(QProcess.MergedChannels)
        if self._cwd:
            process.setWorkingDirectory(self._cwd)
        process.readyReadStandardOutput.connect(lambda p=process: self._drain(p))
        process.finished.connect(lambda code, status, p=process, c=command: self._done(code, status, p, c))
        process.errorOccurred.connect(lambda err, p=process, c=command: self._error(err, p, c))
        self._process = process
        self.started.emit(command)
        process.start(command[0], command[1:])

    def _drain(self, process: QProcess) -> None:
        data = process.readAllStandardOutput().data()
        if data:
            self.output.emit(data.decode("utf-8", errors="replace"))

    def _done(self, code: int, status, process: QProcess, command: list[str]) -> None:
        # Flush any remaining output before emitting finished.
        self._drain(process)
        process.deleteLater()
        self._process = None
        if self._aborted:
            self.finished.emit(code, "aborted")
            self.sequence_finished.emit(code)
            return
        if status == QProcess.CrashExit:
            self.finished.emit(code, "crash")
            self.sequence_finished.emit(code or 1)
            return
        reason = "ok" if code == 0 else "error"
        self.finished.emit(code, reason)
        if code != 0:
            self._queue.clear()
            self.sequence_finished.emit(code)
            return
        if self._queue:
            self._start_next()
        else:
            self.sequence_finished.emit(0)

    def _error(self, err, process: QProcess, command: list[str]) -> None:
        # Covers "failed to start" (e.g. binary not found).  ``_done`` also
        # fires on some platforms, so guard against double emission.
        if self._process is None:
            return
        self.output.emit(f"[process error] {err}: {' '.join(command)}\n")
