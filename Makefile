all:
	gcc -D_GNU_SOURCE sparse_util.c -o sparse_util
clean:
	rm sparse_util
