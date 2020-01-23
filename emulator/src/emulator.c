#define _CRT_SECURE_NO_DEPRECATE
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

#include <termios.h>
#include <sys/select.h>

typedef union genreg16 {
	short word;
	char bytes[2];
	unsigned short adr;
}GenReg16;

typedef union flags {
	short word;
	char bytes[2];
	struct Part {
		unsigned short z : 1;
		unsigned short o : 1;
		unsigned short c : 1;
		unsigned short n : 1;
		unsigned short un : 9;
		unsigned short Tr : 1;
		unsigned short Tl : 1;
		unsigned short I : 1;
	} part;
}Flags;

typedef struct cpuRegs {
	GenReg16 r[8]; // r7-PC, r6-SP
	Flags psw;
}Cpu;


typedef union InstrDescr
{
struct Part1 {
	unsigned char un : 2;
	unsigned char s : 1;
	unsigned char oc : 5; //opcode
} part1;
unsigned char full;
} InstrDescr;

typedef union OpDescr
{
struct Part2 {
	unsigned char lh : 1;
	unsigned char r : 4;
	unsigned char am : 3; //nacin adresiranja
} part;
unsigned char full;
} OpDescr;


typedef struct TS {
	char name[100];
	int section; // rbr sekcije
	int value; // vrednost(pomeraj u odnosu na pocetak sekcije)
	int local; // 0-> global; -1->extern; 1->local;
	int rbr;
	int size; // ukupna velicina sekcije
	int attributes;
	int position;

}symTabEntry;


typedef struct rel {
	int offset; // [LC]
	char type; // A-apsolutno ili R-PC Relativno
	int value;
	int numFile;
	struct relEntry * next;
}relEntry;

typedef struct file {
	symTabEntry* symTable;
	relEntry **relHeads;
	int* relCnt;
	char **binaryContent;
	int tailSymTable; 
	int cntSection;
}File;


File* files;
int fileCnt = 0;
char **sections;
int kap = 10, numSec = 0;

relEntry **relHeads, **relTails;
char ** binaryContent;
int *sectionLC;

int *starts, *ends;

char mem[65535];

Cpu cpu;
InstrDescr opcode;
OpDescr dst, src;
int interrupt=0, timer=0, terminal=0,badInstruction=0, out = 0;
GenReg16 operand1, operand2;
GenReg16 pom;
GenReg16 *pokOp1, *pokOp2;

unsigned short ind = 0;

clock_t start, end;
double cpu_time_used;
float timerTimes[] = { 0.5,1,1.5,2,5,10,30,60 };





// terminal
struct termios prev_termios;

static void emu_init_terminal()
{
	struct termios tmp;

	if (tcgetattr(fileno(stdin), &tmp) == -1) {
		printf("Failed to read terminal attributes.\n");
		exit(666);
	}

	prev_termios = tmp;
	tmp.c_lflag &= ~ECHO;
	tmp.c_lflag &= ~ICANON;
	fflush(stdout);
	if (tcsetattr(fileno(stdin), TCSANOW, &tmp) == -1) {
		printf("Failed to set terminal attributes.\n");
		exit(666);
	}
}


static void emu_restore_terminal()
{
	if (tcsetattr(fileno(stdin), TCSANOW, &prev_termios) == -1) {
		printf("Failed to restore terminal.\n");
	}
}
int emu_wait_kbhit()
{
	int STDIN_FILENO = fileno(stdin);
	// timeout structure passed into select
	struct timeval tv;
	// fd_set passed into select
	fd_set fds;
	// Set up the timeout.
	tv.tv_sec = 0;
	tv.tv_usec = 0;

	// Zero out the fd_set - make sure it's pristine
	FD_ZERO(&fds);
	// Set the FD that we want to read
	FD_SET(STDIN_FILENO, &fds); //STDIN_FILENO is 0
								// select takes the last file descriptor value + 1 in the fdset to check,
								// the fdset for reads, writes, and errors.  We are only passing in reads.
								// the last parameter is the timeout.  select will return if an FD is ready or 
								// the timeout has occurred
	select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
	// return 0 if STDIN is not ready to be read.
	return FD_ISSET(STDIN_FILENO, &fds);
}




int convertToAdress(char* num) {
	int i = 0, rez = 0;

	if ((num[0] == '0') && (num[1] == 'x')) {
		//hexa
		i = 2;
		while (num[i] != '\0') {
			if (!(num[i] >= '0' && num[i] <= '9') && !(num[i] >= 'A' && num[i] <= 'F') && !(num[i] >= 'a' && num[i] <= 'f')) {
				return -1;
			}
			i++;
		}

		i = 2;
		int pom;
		while (num[i] != '\0') {
			if (num[i] >= '0' && num[i] <= '9')
				pom = num[i] - '0';
			if (num[i] >= 'A' && num[i] <= 'F')
				pom = num[i] - 'A' + 10;
			if (num[i] >= 'a' && num[i] <= 'f')
				pom = num[i] - 'a' + 10;

			rez = rez << 4;
			rez = rez | pom;
			i++;
		}
		return rez;
	}

	else {
		printf("Wrong adress format!\n");
		system("pause");
		exit(-1);
	}

	return 0;
}

int dodaj(char* sec) {
	int i = 0;
	while (i < numSec) {
		if (!strcmp(sec, sections[i])) {
			return i;
		}
		i++;
	}

	sections[numSec] = malloc(sizeof(char) * 100);
	strcpy(sections[numSec], sec);
	sectionLC[numSec] = -1;
	numSec++;

	if (numSec == kap) {
		sections = realloc(sections, sizeof(char*)*kap * 2);
		sectionLC = realloc(sectionLC, sizeof(int)*kap * 2);
		relHeads = realloc(relHeads,sizeof(relEntry*)*kap*2);
		relTails = realloc(relTails,sizeof(relEntry*)*kap*2);
		binaryContent = realloc(binaryContent,sizeof(char*)*kap*2);
		kap = kap * 2;
	}

	return numSec - 1;
}


void processPlaceParametar(char* arg) {
	int i = 0, j = 0;;
	char  pom[30],br[30];


	while (arg[i] != '=') {
		pom[i] = arg[i];
		i = i + 1;
	}
	pom[i] = '\0';
	if (strcmp(pom, "-place")) {
		printf("Wrong argument!\n");
		system("pause");
		exit(-1);
	}

	i = i + 1;
	j = 0;
	while (arg[i] != '@') {
		pom[j] = arg[i];
		j++;
		i++;
	}
	i++;
	pom[j] = '\0';

	j = 0;
	while (arg[i] != '\0') {
		br[j++] = arg[i++];
	}

	br[j] = '\0';

	int adr = convertToAdress(br);

	int ind=dodaj(pom);

	sectionLC[ind] = adr;
	
}



int findIndex(char *name,File file) {
	int i = 0;
	while (i < file.tailSymTable) {
		if (!strcmp(name, file.symTable[i].name)) {
			return i;
		}
		i++;
	}

	return -1;
}

void processInputFile(char *in) {
	FILE* myFile;
	myFile=fopen(in, "r");

	if (myFile == NULL) {
		printf("File %s doesn't exist\n", in);
		exit(-1);
	}
	int tail,i;

	fscanf(myFile, "%d\n", &tail);
	files[fileCnt].symTable = malloc(sizeof(symTabEntry)*tail);
	files[fileCnt].tailSymTable=tail;


	i = 0;
	while (i < tail) {
		fscanf(myFile, "%d %s %d %d %d %d %d %d\n", &files[fileCnt].symTable[i].rbr, &files[fileCnt].symTable[i].name, &files[fileCnt].symTable[i].section, &files[fileCnt].symTable[i].value, &files[fileCnt].symTable[i].local, &files[fileCnt].symTable[i].size, &files[fileCnt].symTable[i].attributes, &files[fileCnt].symTable[i].position);
		i++;
	}


	fscanf(myFile, "%d\n", &tail);
	files[fileCnt].relHeads = malloc(sizeof(relEntry*)*tail);
	files[fileCnt].relCnt = malloc(sizeof(int)*tail);
	files[fileCnt].binaryContent = malloc(sizeof(char*)*tail);
	files[fileCnt].cntSection = tail;


	i = 0;
	char ime[100];
	int j = 0;
	while (i < files[fileCnt].cntSection) {
		j = 0;
		fscanf(myFile, "%s\n", &ime);
		int ind = findIndex(ime, files[fileCnt]);

		

		fscanf(myFile, "%d\n", &files[fileCnt].relCnt[i]);

		files[fileCnt].relHeads[i] = malloc(sizeof(relEntry)*files[fileCnt].relCnt[i]);
		files[fileCnt].binaryContent[i] = malloc(sizeof(char) * files[fileCnt].symTable[ind].size);

		while (j < files[fileCnt].relCnt[i]) {
			fscanf(myFile, "%d %c %d\n", &files[fileCnt].relHeads[i][j].offset, &files[fileCnt].relHeads[i][j].type, &files[fileCnt].relHeads[i][j].value);
			files[fileCnt].relHeads[i][j].numFile = fileCnt;
			files[fileCnt].relHeads[i][j].next = NULL;
			j++;
		}

		j = 0;

		while (j < files[fileCnt].symTable[ind].size) {
			fscanf(myFile, "%02hhX", &files[fileCnt].binaryContent[i][j]);
			j++;
		}

		dodaj(files[fileCnt].symTable[ind].name);
		i++;
	}

	fileCnt++;

	fclose(myFile);

}

int checkAlreadyDefined(char* symName, int fileNum) {
	int i = 0,j=0;

	while (i < fileCnt) {
		if (i == fileNum) {
			i++;
			continue;
		}

		j = 0;
		while (j < files[i].tailSymTable) {
			if ((!strcmp(symName, files[i].symTable[j].name)) && (files[i].symTable[j].local == 0) && (files[i].symTable[j].section > 0)) {
				return -1;
			}
			j++;
		}
		i++;
	}

	return 1;
}

int checkNotDefined(char* symName, int fileNum) {
	int i = 0, j = 0;

	while (i < fileCnt) {
		if (i == fileNum) {
			i++;
			continue;
		}

		j = 0;
		while (j < files[i].tailSymTable) {
			if ((!strcmp(symName, files[i].symTable[j].name)) && (files[i].symTable[j].local == 0) && (files[i].symTable[j].section > 0)) {
				return 1;
			}
			j++;
		}
		i++;
	}

	return -1;

}

int findSimbol(char* symName) {
	int i = 0, j = 0;

	while (i < fileCnt) {

		j = 0;
		while (j < files[i].tailSymTable) {
			if ((!strcmp(symName, files[i].symTable[j].name)) && (files[i].symTable[j].local == 0) && (files[i].symTable[j].section > 0)) {
				return  files[i].symTable[j].value + files[i].symTable[files[i].symTable[j].section].value;
			}
			j++;
		}
		i++;
	}
	return -1;
}

void linkFiles() {
	int i = 0,j=0;
	char* sec;
	while (i < numSec) {
		sec = sections[i];
		j = 0;

		relHeads[i] = NULL;
		relTails[i] = NULL;
		binaryContent[i] = malloc(sizeof(char) * 65535);

		int m; int pos;
		while (j < fileCnt) {
			int ind = findIndex(sec, files[j]);
			if (ind == -1) {
				j++;
				continue;
			}

			pos = files[j].symTable[ind].position;

			files[j].symTable[ind].value = sectionLC[i];
		

			m = 0;
			int br = files[j].relCnt[pos];
			while (m < br) {
				relEntry *novi = &files[j].relHeads[pos][m];
				if (relHeads[i] == NULL) {
					relHeads[i] = novi;
				}
				else {
					relTails[i]->next = novi;
				}

				relTails[i] = novi;
				novi->offset = novi->offset + sectionLC[i];
				novi->numFile = j;
				m++;
			}

			m = 0;
			while (m < files[j].symTable[ind].size) {
				binaryContent[i][sectionLC[i]] = files[j].binaryContent[pos][m];
				m++;
				sectionLC[i]++;
			}
			j++;

		}

		i++;
	}

	int n = 0,rez=0;
	i = 0;
	j = 0;

	while (i < fileCnt) {
		j = 0;
		while (j < files[i].tailSymTable) {
			//global
			if ((files[i].symTable[j].local == 0) && (files[i].symTable[j].section > 0)) {
				rez=checkAlreadyDefined(files[i].symTable[j].name,i);
				if (rez == -1) {
					printf("Multiple definition on global simbol\n");
					system("pause");
					exit(-1);
				}
			}


			//extern
			if ((files[i].symTable[j].local == 0) && (files[i].symTable[j].section == 0)) {
				rez = checkNotDefined(files[i].symTable[j].name,i);
				if (rez == -1) {
					printf("Extern simbol not defined\n");
					system("pause");
					exit(-1);
				}
			}

			j++;

		}

		i++;
	}

	i = 0;
	relEntry* tek;
	symTabEntry* pom;
	int adr,masa;
	int mask= 0xFF;
	char lower, higher;
	int l, h;
	while (i < numSec) {
	//PROVERI OVO!!!
		tek = relHeads[i];
		while (tek != NULL) {
			pom = files[tek->numFile].symTable;
			if (pom[tek->value].local == 0) {
				adr=findSimbol(pom[tek->value].name);
				if (tek->type == 'A') {
					lower = adr&mask;
					adr = adr >> 8;
					higher = adr&mask;
					binaryContent[i][tek->offset] = lower;
					binaryContent[i][tek->offset+1] = higher;
				}
				else {
					masa = adr - tek->offset;
					l = binaryContent[i][tek->offset];
					h = binaryContent[i][tek->offset + 1];
					h = h << 8;
					h = h | (l & mask);
					masa = masa + h;
					adr = masa;

					lower = adr&mask;
					adr = adr >> 8;
					higher = adr&mask;
					binaryContent[i][tek->offset] = lower;
					binaryContent[i][tek->offset + 1] = higher;
				}
			}

			else {
				adr = pom[tek->value].value;//vrednost sekcije
				if (tek->type == 'A') {
					h = 0;
					l = 0;
					l = binaryContent[i][tek->offset];
					h = binaryContent[i][tek->offset + 1];
					h = h << 8;
					h = h | (l & mask);
					adr = adr+h;

					lower = adr&mask;
					adr = adr >> 8;
					higher = adr&mask;
					binaryContent[i][tek->offset] = lower;
					binaryContent[i][tek->offset + 1] = higher;
				}

				else {
					masa = pom[tek->value].value;
					l = binaryContent[i][tek->offset];
					h = binaryContent[i][tek->offset + 1];
					h = h << 8;
					h = h | (l & mask);
					masa = masa + h;
					adr = masa-tek->offset;

					lower = adr&mask;
					adr = adr >> 8;
					higher = adr&mask;
					binaryContent[i][tek->offset] = lower;
					binaryContent[i][tek->offset + 1] = higher;
				}
			}

			tek = tek->next;
		}

		i++;
	}
	
}

int calculateEndAdress(int numS) {
	int i = 0,j=0;
	int cnt = 0;

	symTabEntry* pom;
	while (i < fileCnt) {
		pom = files[i].symTable;
		j = 0;
		while (j < files[i].tailSymTable) {
			if (!strcmp(sections[numS], pom[j].name)) {
				cnt = cnt + pom[j].size; 
			}
			j++;
		}
		i++;
	}

	return cnt;
}

void checkMemory() {
	int i = 0;
	int max = 0;

	while (i < numSec) {
		if (sectionLC[i] == -1) {
			i = i + 1;
			continue;
		}

		starts[i] = sectionLC[i];
		ends[i] = calculateEndAdress(i);
		ends[i] = ends[i] + starts[i];

		if (ends[i] > max) {
			max = ends[i];
		}

		i++;
	}

	i = 0;
	while(i < numSec) {
		if (sectionLC[i] != -1) {
			i = i + 1;
			continue;
		}

		sectionLC[i] = starts[i] = max;
		ends[i] = max + calculateEndAdress(i);

		max = ends[i];

		if (max > (65535 - 256)) {
			printf("Error memory exception\n");
			system("pause");
			exit(-1);
		}

		i++;
	}

	int start1, start2, end1, end2;

	int m = 0, n = 0;

	while (m < numSec) {
		start1 = starts[m];
		end1 = ends[m];
		n = 0;
		while (n < numSec) {
			if (n == m) {
				n++;
				continue;
			}
			start2=starts[n];
			end2 = ends[n];

			if ((start2 >= start1 && end1 > start2) || (start1 >= start2 && end2 > start1)) {
				printf("Section overlap!\n");
				system("pause");
				exit(-1);
			}
			n++;
		}
		m++;
	}


}

void fillMemory() {
	int i = 0,j=0,cnt;

	while (i < numSec){

		for (j=starts[i]; j < ends[i]; j++) {
			mem[j] = binaryContent[i][j];
		}
		i++;
	}
}

void takeDst() {
	unsigned short ind = 0;
	dst.full = mem[cpu.r[7].adr++];
	operand1.word = 0;
	pom.word = 0;
	switch (dst.part.am) {
	case 0://neposredno
		// if cmp
		if (opcode.part1.oc == 9) {
			if (opcode.part1.s == 0) {
				operand1.bytes[0] = mem[cpu.r[7].adr++];
				pokOp1 = &operand1.bytes[0];
			}
			else {
				operand1.bytes[0] = mem[cpu.r[7].adr++];
				operand1.bytes[1] = mem[cpu.r[7].adr++];
				pokOp1 = &operand1.word;
			}
		}
		else {
			interrupt = 1;
		}
		break;
	case 1://registarsko direktno
		if (opcode.part1.s == 0) {
			if (dst.part.r == 0xf) {
				operand1.bytes[0] = cpu.psw.bytes[dst.part.lh];
				pokOp1 = &cpu.psw.bytes[dst.part.lh];
			}
			else {
				operand1.bytes[0] = cpu.r[dst.part.r].bytes[dst.part.lh];
				pokOp1 = &cpu.r[dst.part.r].bytes[dst.part.lh];
			}
		}
		else {
			if (dst.part.r == 0xf) {
				operand1.word = cpu.psw.word;
				pokOp1 = &cpu.psw.word;
			}
			else {
				operand1.word = cpu.r[dst.part.r].word;
				pokOp1 = &cpu.r[dst.part.r].word;
			}
		}

		break;
	case 2://registarsko indirektno bez pomeraja
		if (opcode.part1.s == 0) {
			operand1.bytes[0] = mem[cpu.r[dst.part.r].adr];
			pokOp1 = &mem[cpu.r[dst.part.r].adr];
			if ((unsigned)0xFF00 == cpu.r[dst.part.r].adr) {
				out = 1;
			}
		}
		else {
			operand1.bytes[0] = mem[cpu.r[dst.part.r].adr];
			operand1.bytes[1] = mem[cpu.r[dst.part.r].adr +1];
			pokOp1 = &mem[cpu.r[dst.part.r].adr];
		}

		break;
	case 3://reg ind sa 8-bitnim pomerajem
		ind = cpu.r[dst.part.r].adr + mem[cpu.r[7].adr++];
		if (opcode.part1.s == 0) {
			operand1.bytes[0] = mem[ind];
			pokOp1 = &mem[ind];
			if ((unsigned)0xFF00 ==  ind) {
				out = 1;
			}
		}
		else {
			operand1.bytes[0] = mem[ind];
			operand1.bytes[1] = mem[ind + 1];
			pokOp1 = &mem[ind];
			if ((unsigned)0xFF00 == ind) {
				out = 1;
			}
		}
		break;
	case 4://reg ind sa 16-bitnim pomerajem
		pom.bytes[0]=mem[cpu.r[7].adr++];
		pom.bytes[1]=mem[cpu.r[7].adr++];
		ind = cpu.r[dst.part.r].adr + pom.word;
		if (opcode.part1.s == 0) {
			operand1.bytes[0] = mem[ind];
			pokOp1 = &mem[ind];
			if ((unsigned)0xFF00 == ind) {
				out = 1;
			}
		}
		else {
			operand1.bytes[0] = mem[ind];
			operand1.bytes[1] = mem[ind + 1];
			pokOp1 = &mem[ind];
			if ((unsigned)0xFF00 == ind) {
				out = 1;
			}
		}
		break;
	case 5://memorijsko direktno
		pom.bytes[0] = mem[cpu.r[7].adr++];
		pom.bytes[1] = mem[cpu.r[7].adr++];
		ind = pom.word;
		if (opcode.part1.s == 0) {
			operand1.bytes[0] = mem[ind];
			pokOp1 = &mem[ind];
			if ((unsigned)0xFF00 == ind) {
				out = 1;
			}
		}
		else {
			operand1.bytes[0] = mem[ind];
			operand1.bytes[1] = mem[ind + 1];
			pokOp1 = &mem[ind];
			if ((unsigned)0xFF00 == ind) {
				out = 1;
			}
		}
		break;
	}
}

void takeSrc() {
	
	src.full = mem[cpu.r[7].adr++];
	operand2.word = 0;
	pom.word = 0;
	switch (src.part.am) {
	case 0://neposredno
		if (opcode.part1.s == 0) {
			operand2.bytes[0] = mem[cpu.r[7].adr++];
			pokOp2 = &operand2.bytes[0];
		}
		else {
			operand2.bytes[0] = mem[cpu.r[7].adr++];
			operand2.bytes[1] = mem[cpu.r[7].adr++];
			pokOp2 = &operand2.word;
		}

		break;
	case 1://registarsko direktno
		if (opcode.part1.s == 0) {
			operand2.bytes[0] = cpu.r[src.part.r].bytes[src.part.lh];
			pokOp2 = &cpu.r[src.part.r].bytes[src.part.lh];
		}
		else {
			operand2.word = cpu.r[src.part.r].word;
			pokOp2 = &cpu.r[src.part.r].word;
		}

		break;
	case 2://registarsko indirektno bez pomeraja
		if (opcode.part1.s == 0) {
			operand2.bytes[0] = mem[cpu.r[src.part.r].adr];
			pokOp2 = &mem[cpu.r[src.part.r].adr];
		}
		else {
			operand2.bytes[0] = mem[cpu.r[src.part.r].adr];
			operand2.bytes[1] = mem[cpu.r[src.part.r].adr + 1];
			pokOp2 = &mem[cpu.r[src.part.r].adr];
		}

		break;
	case 3://reg ind sa 8-bitnim pomerajem
		ind = cpu.r[src.part.r].adr + mem[cpu.r[7].adr++];
		if (opcode.part1.s == 0) {
			operand2.bytes[0] = mem[ind];
			pokOp2 = &mem[ind];
		}
		else {
			operand2.bytes[0] = mem[ind];
			operand2.bytes[1] = mem[ind + 1];
			pokOp2 = &mem[ind];
		}
		break;
	case 4://reg ind sa 16-bitnim pomerajem
		pom.bytes[0] = mem[cpu.r[7].adr++];
		pom.bytes[1] = mem[cpu.r[7].adr++];
		ind = cpu.r[src.part.r].adr + pom.word;
		if (opcode.part1.s == 0) {
			operand2.bytes[0] = mem[ind];
			pokOp2 = &mem[ind];
		}
		else {
			operand2.bytes[0] = mem[ind];
			operand2.bytes[1] = mem[ind + 1];
			pokOp2 = &mem[ind];
		}
		// if jumps
		if (opcode.part1.oc >= 19 && opcode.part1.oc <= 23) {
			// pc relative jump
			pokOp2 = &ind;
			pokOp2->word = pom.word + cpu.r[7].adr;
		}
		break;
	case 5://memorijsko direktno
		pom.bytes[0] = mem[cpu.r[7].adr++];
		pom.bytes[1] = mem[cpu.r[7].adr++];
		ind = pom.word;
		if (opcode.part1.s == 0) {
			operand2.bytes[0] = mem[ind];
			pokOp2 = &mem[ind];
		}
		else {
			operand2.bytes[0] = mem[ind];
			operand2.bytes[1] = mem[ind + 1];
			pokOp2 = &mem[ind];
		}
		break;
	}
}

void processXchg() {

	takeDst();
	takeSrc();
	if (src.part.am == 0) {
		interrupt = 1;
		return;
	}


	if (opcode.part1.s == 0) {
		(*pokOp1).bytes[0] = operand2.bytes[0];
		(*pokOp2).bytes[0] = operand1.bytes[0];
	}
	else {
		(*pokOp1).bytes[0]= operand2.bytes[0];
		(*pokOp1).bytes[1] = operand2.bytes[1];
		(*pokOp2).bytes[0] = operand1.bytes[0];
		(*pokOp2).bytes[1] = operand1.bytes[1];
	}

}

void processInt(){

	takeSrc();

	mem[--cpu.r[6].adr] = cpu.r[7].bytes[1];
	mem[--cpu.r[6].adr] = cpu.r[7].bytes[0];
	mem[--cpu.r[6].adr] = cpu.psw.bytes[1];
	mem[--cpu.r[6].adr] = cpu.psw.bytes[0];

	unsigned short tek = ((operand2.word % 8) * 2);
	cpu.r[7].bytes[0] = mem[tek];
	cpu.r[7].bytes[1] = mem[tek+1];
	cpu.psw.part.I = 0;

}

void processMov() {

	takeDst();
	takeSrc();

	cpu.psw.part.z = 0;
	cpu.psw.part.n = 0;
	

	if (opcode.part1.s == 0) {
		(*pokOp1).bytes[0] = (*pokOp2).bytes[0];

		if ((*pokOp1).bytes[0] == 0) {
			cpu.psw.part.z = 1;
		}

		if ((*pokOp1).bytes[0] < 0) {
			cpu.psw.part.n = 1;
		}

	}
	else {
		(*pokOp1).word = (*pokOp2).word;

		if ((*pokOp1).word == 0) {
			cpu.psw.part.z = 1;
		}

		if ((*pokOp1).word < 0) {
			cpu.psw.part.n = 1;
		}
	}


}

void processAdd() {

	takeDst();
	takeSrc();

	cpu.psw.part.z = 0;
	cpu.psw.part.o = 0;
	cpu.psw.part.c = 0;
	cpu.psw.part.n = 0;
	
	GenReg16 pom;
	pom.word = 0;


	if (opcode.part1.s == 0) {
		pom.bytes[0] = (*pokOp1).bytes[0];

		(*pokOp1).bytes[0] = (*pokOp1).bytes[0]+(*pokOp2).bytes[0];

		if ((*pokOp1).bytes[0] == 0) {
			cpu.psw.part.z = 1;
		}

		if ((*pokOp1).bytes[0] < 0) {
			cpu.psw.part.n = 1;
		}

		if ((unsigned)(pom.bytes[0]) > (unsigned)((*pokOp1).bytes[0])) {
			cpu.psw.part.c = 1;
		}

		if ((pom.bytes[0] > 0 && (*pokOp2).bytes[0] > 0 && (*pokOp1).bytes[0] < 0) || (pom.bytes[0] < 0 && (*pokOp2).bytes[0] < 0 && (*pokOp1).bytes[0] > 0)) {
			cpu.psw.part.o = 1;
		}

	}
	else {
		(*pokOp1).word = (*pokOp1).word+(*pokOp2).word;

		if ((*pokOp1).word == 0) {
			cpu.psw.part.z = 1;
		}

		if ((*pokOp1).word < 0) {
			cpu.psw.part.n = 1;
		}

		if ((unsigned)(pom.word) > (unsigned)((*pokOp1).word)) {
			cpu.psw.part.c = 1;
		}

		if ((pom.word> 0 && (*pokOp2).word > 0 && (*pokOp1).word < 0) || (pom.word < 0 && (*pokOp2).word < 0 && (*pokOp1).word > 0)) {
			cpu.psw.part.o = 1;
		}
	}


}

void processSub() {

	takeDst();
	takeSrc();

	cpu.psw.part.z = 0;
	cpu.psw.part.o = 0;
	cpu.psw.part.c = 0;
	cpu.psw.part.n = 0;

	GenReg16 pom;
	pom.word = 0;


	if (opcode.part1.s == 0) {
		pom.bytes[0] = (*pokOp1).bytes[0];

		(*pokOp1).bytes[0] = (*pokOp1).bytes[0] - (*pokOp2).bytes[0];

		if ((*pokOp1).bytes[0] == 0) {
			cpu.psw.part.z = 1;
		}

		if ((*pokOp1).bytes[0] < 0) {
			cpu.psw.part.n = 1;
		}

		if ((unsigned)(pom.bytes[0]) < (unsigned)((*pokOp1).bytes[0])) {
			cpu.psw.part.c = 1;
		}

		if ((pom.bytes[0] > 0 && (*pokOp2).bytes[0] > 0 && (*pokOp1).bytes[0] < 0) || (pom.bytes[0] < 0 && (*pokOp2).bytes[0] < 0 && (*pokOp1).bytes[0] > 0)) {
			cpu.psw.part.o = 1;
		}

	}
	else {
		(*pokOp1).word = (*pokOp1).word - (*pokOp2).word;

		if ((*pokOp1).word == 0) {
			cpu.psw.part.z = 1;
		}

		if ((*pokOp1).word < 0) {
			cpu.psw.part.n = 1;
		}

		if ((unsigned)(pom.word) < (unsigned)((*pokOp1).word)) {
			cpu.psw.part.c = 1;
		}

		if ((pom.word> 0 && (*pokOp2).word > 0 && (*pokOp1).word < 0) || (pom.word < 0 && (*pokOp2).word < 0 && (*pokOp1).word > 0)) {
			cpu.psw.part.o = 1;
		}
	}


}

void processMul() {
	takeDst();
	takeSrc();

	cpu.psw.part.z = 0;
	cpu.psw.part.n = 0;

	if (opcode.part1.s == 0) {
		(*pokOp1).bytes[0] = (*pokOp1).bytes[0] * (*pokOp2).bytes[0];

		if ((*pokOp1).bytes[0] == 0) {
			cpu.psw.part.z = 1;
		}

		if ((*pokOp1).bytes[0] < 0) {
			cpu.psw.part.n = 1;
		}
	}
	else {
		(*pokOp1).word = (*pokOp1).word * (*pokOp2).word;

		if ((*pokOp1).word == 0) {
			cpu.psw.part.z = 1;
		}

		if ((*pokOp1).word < 0) {
			cpu.psw.part.n = 1;
		}
	}

}

void processDiv() {

	takeDst();
	takeSrc();

	cpu.psw.part.z = 0;
	cpu.psw.part.n = 0;

	if (opcode.part1.s == 0) {
		// div by 0
		if ((*pokOp2).bytes[0] == 0) {
			badInstruction = 1;
			return;
		}
		(*pokOp1).bytes[0] = (*pokOp1).bytes[0] / (*pokOp2).bytes[0];

		if ((*pokOp1).bytes[0] == 0) {
			cpu.psw.part.z = 1;
		}

		if ((*pokOp1).bytes[0] < 0) {
			cpu.psw.part.n = 1;
		}
	}
	else {
		// div by 0
		if ((*pokOp2).word == 0) {
			badInstruction = 1;
			return;
		}
		(*pokOp1).word = (*pokOp1).word / (*pokOp2).word;

		if ((*pokOp1).word == 0) {
			cpu.psw.part.z = 1;
		}

		if ((*pokOp1).word < 0) {
			cpu.psw.part.n = 1;
		}
	}

}

void processCmp() {

	takeDst();
	takeSrc();

	cpu.psw.part.z = 0;
	cpu.psw.part.o = 0;
	cpu.psw.part.c = 0;
	cpu.psw.part.n = 0;

	GenReg16 pom;
	pom.word = 0;


	if (opcode.part1.s == 0) {
		pom.bytes[0] = (*pokOp1).bytes[0];

		pom.bytes[0] = (*pokOp1).bytes[0] - (*pokOp2).bytes[0];

		if (pom.bytes[0] == 0) {
			cpu.psw.part.z = 1;
		}

		if (pom.bytes[0]  < 0) {
			cpu.psw.part.n = 1;
		}

		if ((unsigned)((*pokOp1).bytes[0]) < (unsigned)((pom.bytes[0]))){
			cpu.psw.part.c = 1;
		}

		if (((*pokOp1).bytes[0]>0 && (*pokOp2).bytes[0] > 0 && pom.bytes[0] < 0) || ((*pokOp1).bytes[0]<0 && (*pokOp2).bytes[0] < 0 && pom.bytes[0] > 0)) {
			cpu.psw.part.o = 1;
		}

	}

	else {
		pom.word = (*pokOp1).word;

		pom.word = (*pokOp1).word - (*pokOp2).word;

		if (pom.word == 0) {
			cpu.psw.part.z = 1;
		}

		if (pom.word  < 0) {
			cpu.psw.part.n = 1;
		}

		if ((unsigned)((*pokOp1).word) < (unsigned)((pom.word))) {
			cpu.psw.part.c = 1;
		}

		if (((*pokOp1).word>0 && (*pokOp2).word > 0 && pom.word < 0) || ((*pokOp1).word<0 && (*pokOp2).word < 0 && pom.word > 0)) {
			cpu.psw.part.o = 1;
		}
	}

}

void processNot() {

	takeDst();

	cpu.psw.part.z = 0;
	cpu.psw.part.n = 0;

	if (opcode.part1.s == 0) {
		(*pokOp1).bytes[0] = ~((*pokOp1).bytes[0]);

		if ((*pokOp1).bytes[0] == 0) {
			cpu.psw.part.z = 1;
		}

		if ((*pokOp1).bytes[0] < 0) {
			cpu.psw.part.n = 1;
		}
	}
	else {
		(*pokOp1).word = ~ ((*pokOp1).word);

		if ((*pokOp1).word == 0) {
			cpu.psw.part.z = 1;
		}

		if ((*pokOp1).word < 0) {
			cpu.psw.part.n = 1;
		}
	}
}

void processAnd() {

	takeDst();
	takeSrc();

	cpu.psw.part.z = 0;
	cpu.psw.part.n = 0;

	if (opcode.part1.s == 0) {
		(*pokOp1).bytes[0] = (*pokOp1).bytes[0] & (*pokOp2).bytes[0];

		if ((*pokOp1).bytes[0] == 0) {
			cpu.psw.part.z = 1;
		}

		if ((*pokOp1).bytes[0] < 0) {
			cpu.psw.part.n = 1;
		}
	}
	else {
		(*pokOp1).word = (*pokOp1).word & (*pokOp2).word;

		if ((*pokOp1).word == 0) {
			cpu.psw.part.z = 1;
		}

		if ((*pokOp1).word < 0) {
			cpu.psw.part.n = 1;
		}
	}
}

void processOr() {

	takeDst();
	takeSrc();

	cpu.psw.part.z = 0;
	cpu.psw.part.n = 0;

	if (opcode.part1.s == 0) {
		(*pokOp1).bytes[0] = (*pokOp1).bytes[0] | (*pokOp2).bytes[0];

		if ((*pokOp1).bytes[0] == 0) {
			cpu.psw.part.z = 1;
		}

		if ((*pokOp1).bytes[0] < 0) {
			cpu.psw.part.n = 1;
		}
	}
	else {
		(*pokOp1).word = (*pokOp1).word | (*pokOp2).word;

		if ((*pokOp1).word == 0) {
			cpu.psw.part.z = 1;
		}

		if ((*pokOp1).word < 0) {
			cpu.psw.part.n = 1;
		}
	}

}

void processXor() {
	takeDst();
	takeSrc();

	cpu.psw.part.z = 0;
	cpu.psw.part.n = 0;

	if (opcode.part1.s == 0) {
		(*pokOp1).bytes[0] = (*pokOp1).bytes[0] ^ (*pokOp2).bytes[0];

		if ((*pokOp1).bytes[0] == 0) {
			cpu.psw.part.z = 1;
		}

		if ((*pokOp1).bytes[0] < 0) {
			cpu.psw.part.n = 1;
		}
	}
	else {
		(*pokOp1).word = (*pokOp1).word ^ (*pokOp2).word;

		if ((*pokOp1).word == 0) {
			cpu.psw.part.z = 1;
		}

		if ((*pokOp1).word < 0) {
			cpu.psw.part.n = 1;
		}
	}
}

void processTest() {

	takeDst();
	takeSrc();

	cpu.psw.part.z = 0;
	cpu.psw.part.n = 0;

	GenReg16 pom;
	pom.word = 0;


	if (opcode.part1.s == 0) {
		pom.bytes[0] = (*pokOp1).bytes[0];

		pom.bytes[0] = (*pokOp1).bytes[0] & (*pokOp2).bytes[0];

		if (pom.bytes[0] == 0) {
			cpu.psw.part.z = 1;
		}

		if (pom.bytes[0]  < 0) {
			cpu.psw.part.n = 1;
		}

	}

	else {
		pom.word = (*pokOp1).word;

		(*pokOp1).word = (*pokOp1).word & (*pokOp2).word;

		if (pom.word == 0) {
			cpu.psw.part.z = 1;
		}

		if (pom.word  < 0) {
			cpu.psw.part.n = 1;
		}
		(*pokOp1).word = pom.word;
	}

}

void processShl() {

	takeDst();
	takeSrc();

	cpu.psw.part.z = 0;
	cpu.psw.part.c = 0;
	cpu.psw.part.n = 0;

	GenReg16 pom;
	pom.word = 0;
	short mask = 1;
	mask = mask << 15;

	if (opcode.part1.s == 0) {
		pom.bytes[0] = (*pokOp1).bytes[0];

		(*pokOp1).bytes[0] = (*pokOp1).bytes[0] << (*pokOp2).bytes[0];

		if ((*pokOp1).bytes[0] == 0) {
			cpu.psw.part.z = 1;
		}

		if ((*pokOp1).bytes[0] < 0) {
			cpu.psw.part.n = 1;
		}

		if ((pom.bytes[0] == (*pokOp1).bytes[0]<<((*pokOp2).bytes[0]-1))&mask) {
			cpu.psw.part.c = 1;
		}
	}

	else {
		pom.word = (*pokOp1).word;

		(*pokOp1).word = (*pokOp1).word << (*pokOp2).word;

		if (pom.word == 0) {
			cpu.psw.part.z = 1;
		}

		if (pom.word  < 0) {
			cpu.psw.part.n = 1;
		}

		if ((pom.word == (*pokOp1).word << ((*pokOp2).word - 1))&mask) {
			cpu.psw.part.c = 1;
		}

	}

}

void processShr() {

	takeDst();
	takeSrc();

	cpu.psw.part.z = 0;
	cpu.psw.part.c = 0;
	cpu.psw.part.n = 0;

	GenReg16 pom;
	pom.word = 0;
	short mask = 1;
	
	if (opcode.part1.s == 0) {
		pom.bytes[0] = (*pokOp1).bytes[0];

		(*pokOp1).bytes[0] = (*pokOp1).bytes[0] >> (*pokOp2).bytes[0];

		if ((*pokOp1).bytes[0] == 0) {
			cpu.psw.part.z = 1;
		}

		if ((*pokOp1).bytes[0] < 0) {
			cpu.psw.part.n = 1;
		}

		if ((pom.bytes[0] == (*pokOp1).bytes[0] >> ((*pokOp2).bytes[0] - 1))&mask) {
			cpu.psw.part.c = 1;
		}

	}

	else {
		pom.word = (*pokOp1).word;

		(*pokOp1).word = (*pokOp1).word >> (*pokOp2).word;

		if (pom.word == 0) {
			cpu.psw.part.z = 1;
		}

		if (pom.word  < 0) {
			cpu.psw.part.n = 1;
		}

		if ((pom.word == (*pokOp1).word >> ((*pokOp2).word - 1))&mask) {
			cpu.psw.part.c = 1;
		}
		
	}
}



void processPush() {

	takeSrc();
	mem[--cpu.r[6].adr] = (*pokOp2).bytes[1];
	mem[--cpu.r[6].adr] = (*pokOp2).bytes[0];
}

void processPop() {

	takeDst();
	(*pokOp1).bytes[0] = mem[cpu.r[6].adr++];
	(*pokOp1).bytes[1] = mem[cpu.r[6].adr++];

}


void processJmp() {

	takeSrc();
	cpu.r[7].bytes[0] = (*pokOp2).bytes[0];
	cpu.r[7].bytes[1] = (*pokOp2).bytes[1];

}

void processJeq() {

	takeSrc();

	if (cpu.psw.part.z == 1) {
		cpu.r[7].bytes[0] = (*pokOp2).bytes[0];
		cpu.r[7].bytes[1] = (*pokOp2).bytes[1];
	}
}

void processJne() {

	takeSrc();

	if (cpu.psw.part.z == 0) {
		cpu.r[7].bytes[0] = (*pokOp2).bytes[0];
		cpu.r[7].bytes[1] = (*pokOp2).bytes[1];
	}
}


void processJgt() {

	takeSrc();

	if (!((cpu.psw.part.n ^ cpu.psw.part.o) || cpu.psw.part.z)) {
		cpu.r[7].bytes[0] = (*pokOp2).bytes[0];
		cpu.r[7].bytes[1] = (*pokOp2).bytes[1];
	}
}

void processCall() {

	takeSrc();

	mem[--cpu.r[6].adr] = cpu.r[7].bytes[1];
	mem[--cpu.r[6].adr] = cpu.r[7].bytes[0];

	cpu.r[7].bytes[0] = (*pokOp2).bytes[0];
	cpu.r[7].bytes[1] = (*pokOp2).bytes[1];


}

void processRet() {

	cpu.r[7].bytes[0] = mem[cpu.r[6].adr++];
	cpu.r[7].bytes[1] = mem[cpu.r[6].adr++];

}

void processIret() {

	cpu.psw.bytes[0] = mem[cpu.r[6].adr++];
	cpu.psw.bytes[1] = mem[cpu.r[6].adr++];

	cpu.r[7].bytes[0] = mem[cpu.r[6].adr++];
	cpu.r[7].bytes[1] = mem[cpu.r[6].adr++];
}

void emulatorRun() {

	emu_init_terminal();
	int work = 1;
	GenReg16 npc,timerConfig;
	cpu.r[7].bytes[0] = mem[0];
	cpu.r[7].bytes[1] = mem[1];
	float passTime = 0;

	while (work==1) {
		start = clock();
		npc.adr = cpu.r[7].adr;
		opcode.full = mem[cpu.r[7].adr];
		cpu.r[7].adr++;
		switch (opcode.part1.oc) {
		case 1://halt
			work = 0;
			break;
		case 2://xchg
			processXchg();
			break;
		case 3://int
			processInt();
			break;
		case 4://mov
			processMov();
			break;
		case 5://add
			processAdd();
			break;
		case 6://sub
			processSub();
			break;
		case 7://mul
			processMul();
			break;
		case 8://div
			processDiv();
			break;
		case 9://cmp
			processCmp();
			break;
		case 10://not
			processNot();
			break;
		case 11://and
			processAnd();
			break;
		case 12://or
			processOr();
			break;
		case 13://xor
			processXor();
			break;
		case 14://test
			processTest();
			break;
		case 15://shl
			processShl();
			break;
		case 16://shr
			processShr();
			break;
		case 17://push
			processPush();
			break;
		case 18://pop
			processPop();
			break;
		case 19://jmp
			processJmp();
			break;
		case 20://jeq
			processJeq();
			break;
		case 21://jne
			processJne();
			break;
		case 22://jgt
			processJgt();
			break;
		case 23://call
			processCall();
			break;
		case 24://ret
			processRet();
			break;
		case 25://iret
			processIret();
			break;
		}
		end = clock();
		cpu_time_used = ((double)(end - start)) / CLOCKS_PER_SEC;
		timerConfig.bytes[0] = mem[0xFF10];
		timerConfig.bytes[1] = mem[0xFF11];
		timerConfig.word = timerConfig.word & 7;
		passTime = passTime + cpu_time_used;

		if (passTime >= timerTimes[timerConfig.word]) {
			timer = 1;
			passTime = passTime - timerTimes[timerConfig.word];
		}

		if (out == 1) {
			printf("%c",mem[(unsigned)0xFF00]);
			fflush(stdout);
			out = 0;
		}
	
		if (emu_wait_kbhit()) {
			if (terminal == 0) {	// ako je obradjen prethodni
				mem[(unsigned)0xFF02] = getchar();
				terminal = 1;
			}
		}	

		if (badInstruction == 1) {
			cpu.r[7].adr = npc.adr;

			mem[--cpu.r[6].adr] = cpu.r[7].bytes[1];
			mem[--cpu.r[6].adr] = cpu.r[7].bytes[0];

			mem[--cpu.r[6].adr] = cpu.psw.bytes[1];
			mem[--cpu.r[6].adr] = cpu.psw.bytes[0];

			cpu.r[7].bytes[0]=mem[2];
			cpu.r[7].bytes[1]=mem[3];
			cpu.psw.part.I = 0;
			badInstruction = 0;
		}
		else if ((timer == 1) &&(cpu.psw.part.Tr== 1)&&(cpu.psw.part.I==1)){
			mem[--cpu.r[6].adr] = cpu.r[7].bytes[1];
			mem[--cpu.r[6].adr] = cpu.r[7].bytes[0];

			mem[--cpu.r[6].adr] = cpu.psw.bytes[1];
			mem[--cpu.r[6].adr] = cpu.psw.bytes[0];

			cpu.r[7].bytes[0] = mem[4];
			cpu.r[7].bytes[1] = mem[5];
			cpu.psw.part.I = 0;
			timer = 0;
		}
		else if ((terminal == 1) && (cpu.psw.part.Tl == 1) && (cpu.psw.part.I == 1)) {

			mem[--cpu.r[6].adr] = cpu.r[7].bytes[1];
			mem[--cpu.r[6].adr] = cpu.r[7].bytes[0];

			mem[--cpu.r[6].adr] = cpu.psw.bytes[1];
			mem[--cpu.r[6].adr] = cpu.psw.bytes[0];

			cpu.r[7].bytes[0] = mem[6];
			cpu.r[7].bytes[1] = mem[7];
			cpu.psw.part.I = 0;
			terminal = 0;
		}
	}

	emu_restore_terminal();
}



int main(int argc, char* argv[]) {
	int i = 1;
	int cntF = 0, cntP = 0;

	while (i < argc) {
		if (argv[i][0] == '-') {
			cntP++;
		}
		else {
			cntF++;
		}
		i++;
	}

	
	files = malloc(sizeof(File)*cntF);
	sections = malloc(sizeof(char*)*kap);
	sectionLC = malloc(sizeof(char*)*kap);
	relHeads = malloc(sizeof(relEntry*)*kap);
	relTails = malloc(sizeof(relEntry*)*kap);
	binaryContent = malloc(sizeof(char*)*kap);


	i = 1;
	while (i < argc) {
		if (argv[i][0] == '-') {
			processPlaceParametar(argv[i]);
		}
		else {
			processInputFile(argv[i]);
		}
		i++;
	}

	starts = malloc(sizeof(int)*numSec);
	ends = malloc(sizeof(int)*numSec);

	checkMemory();
	linkFiles();
	fillMemory();

	emulatorRun();

}
