compile:
In the directory of the pintool:
	"make PIN_ROOT=<pin directory>"
The pintool source can be in any directory rather than only a subdir of pin.

run:
In the directory of the pintool:
	"<pin directory>/pin -t <pintool binary directory>/ex1.so -- ./bzip2 -k -v ./input.txt"

Pintool binary directory is usually obj-intel64 in src (when compiling there).


The output is in rtn-output.txt


---
There might be differences when running on different environments because some functions interact
with the OS and their running time depends on the OS values. More than that, the same task might
take different amount of instructions on different platforms because new platforms typically have
new ISAs which may do several operations in one instruction (e.g. copying data using a newer
version of AVX uses larger registers and therefore less copying instructions for the same buffer).
An optimized code can check explictly for a support of the newer ISA and change its behaviour
accordingly.
