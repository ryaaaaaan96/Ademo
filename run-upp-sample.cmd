@echo off
call "%~dp0run-can-sample.cmd" --profile upp --interface can0 --count 60 --interval 20 %*
