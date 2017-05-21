Marina Minkin 307659318
Gil Kupfer 201112919

compile:
In the directory of the pintool:
	"make PIN_ROOT=<pin directory>"
The pintool source can be in any directory rather than only a subdir of pin.

run:
In the directory of the pintool:
	"<pin directory>/pin -t <pintool binary directory>/ex1.so -- ./bzip2 -k -v ./input.txt"

Pintool binary directory is usually obj-intel64 in src (when compiling there).


The output is in rtn-output.txt
