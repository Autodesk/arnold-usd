@REM invokes a local install of scons (forwarding all arguments)

@python -B tools\scons\scons.py --site-dir=tools\scons-custom %*
