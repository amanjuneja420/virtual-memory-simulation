all: master sched mmu process

master: Master.c
	gcc Master.c -o master

sched: sched.c
	gcc sched.c -o sched

mmu: MMU.c
	gcc MMU.c -o mmu

process: process.c
	gcc process.c -o process

clean:
	rm -f master sched mmu process result.txt
