#define _CRT_SECURE_NO_DEPRECATE
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct A {
	char operation[15];
	char dst[100];
	char src[100];
	char label[100];
}Arg;

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
	struct relEntry * next;
}relEntry;


typedef union InstrDescr
{
	struct Part1 {
		unsigned char un : 2;
		unsigned char s :  1;
		unsigned char oc : 5; //opcode
	} part;
	unsigned char full;
} InstrDescr;

typedef union OpDescr
{
	struct Part2{
		unsigned char lh : 1;
		unsigned char r :  4;
		unsigned char am : 3; //nacin adresiranja
	} part;
	unsigned char full;
} OpDescr;



char *directives[] = { ".extern",".global",".equ",".text",".bss",".data",
					   ".section",".byte",".word",".align",".skip" ,".end" };

char *opcode[] = { "halt","xchg","int","mov","add","sub","mul","div","cmp","not",
				"and","or","xor","test","shl","shr","push","pop","jmp","jeq",
				"jne","jgt","call","ret","iret" };


relEntry **relHeads, **relTails;
char ** binaryContent;

int numDirectives = 12;
int numOpcodes = 25;
char line[100];
char c;
FILE *inFile , *outFile;
Arg arg;
int cntLine,error;
symTabEntry *symTable;
int mySection=0,myPosition=0;//br ulaza u symTab, UND=0
int Kap = 50,kapRels=10;
int tailSymTable = 0,tailRels=0;
int LC=0;
int end = 0;
char bw;
int pass; // 1 -first  pass; 2-second pass
InstrDescr instrDescr;
int srcRelLC, dstRelLC, srcRelIndex,dstRelIndex,dstSameSec,srcSameSec;


int checkLabel() {
	if (!(arg.label[0] >= 'a' && arg.label[0] <= 'z') && !(arg.label[0] >= 'A' && arg.label[0] <= 'Z')) {
		return -1;
	}

	int i = 1;
	while (arg.label[i] != '\0') {
		if ((arg.label[i] != '_') && !(arg.label[i] >= '0' && arg.label[i] <= '9') && !(arg.label[0] >= 'a' && arg.label[0] <= 'z') && !(arg.label[0] >= 'A' && arg.label[0] <= 'Z')) {
			return -1;
		}
		i++;
	}

	if (pass == 2) {
		i = 0;
		while (i < tailSymTable) {
			if (!strcmp(symTable[i].name, arg.label)) {
				return 1;
			}
			i++;
		}
		printf("Error: label doesn't exists! Line error: %d\n", cntLine);
		error = 1;
		return -1;
	}
	
	return 1;
}

int isNumber(char* num,int *r) {
	int i = 0,rez=0;

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
		while (num[i] != '\0'){
			if (num[i] >= '0' && num[i] <= '9')
				pom = num[i] - '0';
			if (num[i] >= 'A' && num[i] <= 'F')
				pom= num[i] - 'A' + 10;
			if (num[i] >= 'a' && num[i] <= 'f')
				pom= num[i] - 'a' + 10;
			
			rez = rez << 4;
			rez = rez | pom;
			i++;
		}
	}

	else{
		//decimal
		if (num[0] == '-') {
			i++;
		}
		while (num[i] != '\0') {
			if (!(num[i] >= '0' && num[i] <= '9')){
				return -1;
			}
			i++;
		}

		if (num[0] == '-') {
			rez = atoi(&num[1]);
			rez = 0 - rez;
		}
		else {
			rez = atoi(num);
		}
		
	}
	
	*r = rez;
	return 1;
}
void deleteMultipleSpaces() {
	int i = 0,cnt=0,j=0,t=0;

	while (line[i] != '\0') {
		if (line[i] == '\t') {
			line[i] = ' ';
		}
		i++;
	}

	i = 0;
	while (line[i] != '\0') {
		if (line[i] == ' ') {
			j = i;
			t = i+1;
			while (line[i] == ' ') {
				cnt++;
				i++;
			}
			if (cnt > 1) {
				while (line[i] != '\0') {
					line[j + 1] = line[i];
					j++;
					i++;
				}
				line[++j] = '\0';
			}
			i = t; 
			cnt = 0;
		}
		else {
			i++;
			cnt = 0;
		}

	}
}

void storeWord(int broj) {
	int mask = 0xFF;
	char lower, higher;
	lower = broj&mask;
	broj = broj >> 8;
	higher = broj&mask;
	binaryContent[myPosition][LC] = lower;
	binaryContent[myPosition][LC + 1] = higher;
}

void extractArgs() {
	char pom[100];
	int i = 0,j;
	if (line[i] == ' ') {
		i++;
	}

	j = 0;
	while ((line[i]!=' ')&&(line[i]!=':') && (line[i] != '\0')) {
		pom[j++] = line[i++];
	}
	pom[j] = '\0';

	if (line[i] == ':') {
		i++;
		strcpy(arg.label, pom);
	}
	else {
		i = 0;
	}
	
	if (line[i] == ' ') {
		i++;
	}

	j = 0;
	while (line[i] != ' ' && line[i] != '\0') {
		pom[j++] = line[i++];
	}
	pom[j] = '\0';

	strcpy(arg.operation, pom);

	if (line[i] == ' ') {
		i++;
	}

	j = 0; int f = 0;
	while ((line[i]!=',')&&(line[i]!='\0')){
		pom[j++] = line[i++];
	}
	if (pom[j - 1] == ' ') {
		pom[j - 1] = '\0';
	}
	else{
		pom[j] = '\0';
	}
	strcpy(arg.dst, pom);


	if (line[i] == ',') {
		i++;
	} 

	if (line[i] == ' ') {
		i++;
	}

	j = 0;
	while ((line[i] != ' ') && (line[i] != '\0')) {
		pom[j++] = line[i++];
	}
	pom[j] = '\0';

	strcpy(arg.src, pom);


	if (line[i] == ' ') {
		i++;
	}


	if (line[i] != '\0') {
		printf("Error on Line number: %d  \n" , cntLine);
		error = 1;
	}


}

void readLine() {
	int i = 0,cnt;

	while ((c != '\n') && (c != EOF)) {
		line[i++] = c;
		c = fgetc(inFile);
	}
	line[i] = '\0';
	cnt = i;
	deleteMultipleSpaces();

	i = 1;
	if (line[0] == ' ') {
		while (line[i] != '\0') {
			line[i - 1] = line[i];
			i++;
		}

		line[i - 1] = '\0';
	}

	if (line[cnt - 1] == ' ') {
		line[cnt - 1] = '\0';
	}
	
}

int indexDirective(char *dir) {
	int i = 0;
	for (i = 0; i < numDirectives; i++) {
		if (!strcmp(directives[i], dir)) {
			return i;
		}
	}
	return -1;
}

void processLabelExtern() {
	int i = 0,r=0;

	
	if (arg.label[0] == '\0') {
		printf("Error: label name is empty! Line Error:%d\n",  cntLine);
		error = 1;
		return;
	}

	r = checkLabel();
	if (r == -1) {
		printf("Error invalid label name !  Error line: %d \n" , cntLine);
		error = 1;
		return;
	}
	while (i < tailSymTable) {
		if (!strcmp(symTable[i].name, arg.label)) {
			printf("Error: label already exists! Line error: %d\n" , cntLine);
			error = 1;
			return;
		}
		i++;
	}

	if (i == tailSymTable) {
		strcpy(symTable[tailSymTable].name, arg.label);
		symTable[tailSymTable].local = -1;
		symTable[tailSymTable].rbr = tailSymTable;
		symTable[tailSymTable].section = 0;
		symTable[tailSymTable].value = 0;
		symTable[tailSymTable].attributes = 0;
		symTable[tailSymTable].position = -1;
		symTable[tailSymTable].size = 0;
		tailSymTable++;
		if (tailSymTable == Kap) {
			symTable = realloc(symTable, Kap * 2 * sizeof(symTabEntry));
			Kap = Kap * 2;
		}
	}

}

void processLabelGlobal() {
	int i = 0,r=0;

	if (arg.label[0] == '\0') {
		printf("Error: label name is empty! Line Error: %d\n" , cntLine);
		return;
	}
	r = checkLabel();
	if (r == -1) {
		printf("Error invalid label name !  Error line: %d \n" , cntLine);
		error = 1;
		return;
	}
	while (i < tailSymTable) {
		if (!strcmp(symTable[i].name, arg.label)) {
			if (symTable[i].section>0) {
				symTable[i].local = 0;
				return;
			}
			else {
				if (symTable[i].section == -1) {
					printf("Error: label already exists! Line error: %d\n", cntLine);
					return;
				}
			}

		}
		i++;
	}

	if (i == tailSymTable) {
		strcpy(symTable[tailSymTable].name, arg.label);
		symTable[tailSymTable].local = 0;
		symTable[tailSymTable].rbr = tailSymTable;
		symTable[tailSymTable].section = 0;
		symTable[tailSymTable].value = 0;
		symTable[tailSymTable].attributes = 0;
		symTable[tailSymTable].position = -1;
		symTable[tailSymTable].size = 0;
		tailSymTable++;
		if (tailSymTable == Kap) {
			symTable = realloc(symTable, Kap * 2 * sizeof(symTabEntry));
			Kap = Kap * 2;
		}
	}


}

void processEqu(char* ime,int br){
	int i = 0, r = 0;

	strcpy(arg.label, ime);

	if (arg.label[0] == '\0') {
		printf("Error: label name is empty! Line Error: %d\n", cntLine);
		error = 1;
		return;
	}


	r = checkLabel();
	if (r == -1) {
		printf("Error invalid label name !  Error line:  %d\n", cntLine);
		error = 1;
		return;
	}
	while (i < tailSymTable) {
		if (!strcmp(symTable[i].name, arg.label)) {
			printf("Error: label already exists! Line error: %d\n", cntLine);
			error = 1;
			return;
		}
		i++;
	}

	if (i == tailSymTable) {
		strcpy(symTable[tailSymTable].name, arg.label);
		symTable[tailSymTable].local = 1;
		symTable[tailSymTable].rbr = tailSymTable;
		symTable[tailSymTable].section =-1; // oznaka da je .equ direktiva
		symTable[tailSymTable].value =br;
		symTable[tailSymTable].attributes = 0;
		symTable[tailSymTable].position = -1;
		symTable[tailSymTable].size = 0;
		tailSymTable++;
		if (tailSymTable == Kap) {
			symTable = realloc(symTable, Kap * 2*sizeof(symTabEntry));
			Kap = Kap * 2;
		}
	}
}

void processSection(char * sec,int atr) {
	int i = 0;

	while (i < tailSymTable) {
		if (!strcmp(symTable[i].name, sec)) {
			printf("Error: section already exists! Line error: %d\n", cntLine);
			error = 1;
			return;
		}
		i++;
	}

	strcpy(symTable[tailSymTable].name, sec);
	symTable[tailSymTable].local = 1;
	symTable[tailSymTable].rbr = tailSymTable;
	symTable[tailSymTable].section = tailSymTable; 
	symTable[tailSymTable].value =0;
	symTable[tailSymTable].size = 0;
	symTable[tailSymTable].attributes = atr; // RWX bitovi
	symTable[tailSymTable].position = 0;
	tailSymTable++;
	if (tailSymTable == Kap) {
		symTable = realloc(symTable, Kap * 2 * sizeof(symTabEntry));
		Kap = Kap * 2;
	}

	LC = 0;
	mySection = tailSymTable - 1;
}

void processSection2(char *sec) {
	int i = 0;

	while (i < tailSymTable) {
		if (!strcmp(symTable[i].name, sec)) {
			symTable[i].position = tailRels;
			relHeads[tailRels] = NULL;
			relTails[tailRels] = NULL;
			binaryContent[tailRels] = malloc(sizeof(char) * 10000);
			tailRels++;
			if (tailRels == kapRels) {
				relHeads = realloc(relHeads, sizeof(relEntry*)*kapRels * 2);
				relTails = realloc(relTails, sizeof(relEntry*)*kapRels * 2);
				binaryContent = realloc(binaryContent, sizeof(char*)*kapRels * 2);
				kapRels = kapRels * 2;
			}
			LC = 0;
			mySection = i;
			myPosition = symTable[mySection].position;
			return;
		}
		i++;
	}
}

void processingDirective() {
	char dir[100];
	int i = 0, j;
	if (line[i] == ' ') {
		i++;
	}

	j = 0;
	while ((line[i] != ' ')&&(line[i]!='\0')) {
		dir[j++] = line[i++];
	}
	dir[j] = '\0';

	int s = indexDirective(dir);
	if (s == -1) {
		printf("Error unrecognized directive on line: %d \n", cntLine);
		error = 1;
		return;
	}
	
	if (line[i] == ' ') {
		i++;
	}

	char sim1[300],sim2[300],num[100];
	int k = 0,broj;
	switch (s) {
	case 0: //.extern
		if (pass == 2) {
			break;
		}
		while (line[i] != '\0') {
			k = 0;
			while((line[i] != ',') && (line[i] != '\0') && (line[i] != ' ')) {
				arg.label[k++] = line[i++];
			}
			arg.label[k] = '\0';
			processLabelExtern();

			if (line[i] == ' ') {
				i++;
			}
			if (line[i] == ',') {
				i++;
				if (line[i] == ' ') {
					i++;
				}
			}
		}
		
		break;
	case 1: //.global
		if (pass == 2) {
			break;
		}
		while (line[i] != '\0') {
			k = 0;
			while ((line[i] != ',') && (line[i] != '\0') && (line[i] != ' ')) {
				arg.label[k++] = line[i++];
			}
			arg.label[k] = '\0';
			processLabelGlobal();

			if (line[i] == ' ') {
				i++;
			}
			if (line[i] == ',') {
				i++;
				if (line[i] == ' ') {
					i++;
				}
			}
		}
		break;

	case 2:	//.equ
		if (pass == 2) {
			break;
		}
		k = 0;
		while ((line[i] != ',') && (line[i] != '\0') && (line[i] != ' ')) {
			sim1[k++] = line[i++];
		}
		sim1[k] = '\0';

		int comma = 0;

		if (line[i] == ' ') {
			i++;
		}
		if (line[i] == ',') {
			comma = 1;
			i++;
		}
		if (line[i] == ' ') {
			i++;
		}
		
		if (comma == 0) {
			printf("Comma missing !  Error line: %d \n", cntLine);
			error = 1;
			return;
		}
		k = 0;
		while (line[i] != '\0') {
			sim2[k++] = line[i++];
		}
		sim2[k] = '\0';

		int rez; //alternativa: rez=malloc(sizeof(int));isNumber(sim2,rez)
		int is=isNumber(sim2,&rez);
		if (is == -1){
			printf("Not number!  Error line: %d \n" , cntLine);
			error = 1;
			return;
		}
			
		if (rez < -32768 || rez>32767) {
			printf("Number out of range !  Error line: %d \n" , cntLine);
			error = 1;
			return;
		}
		processEqu(sim1,rez);

		break;
	case 3: //.text
		if (pass == 2) {
			processSection2(".text");
		}
		else{
			symTable[mySection].size = LC;
			processSection(".text", 5);
		}
		break;
	case 4: //.bss
		if (pass == 2) {
			processSection2(".bss");
		}
		else {
			symTable[mySection].size = LC;
			processSection(".bss", 6);
		}
		break;
	case 5://.data
		if (pass == 2) {
			processSection2(".data");
		}
		else {
			symTable[mySection].size = LC;
			processSection(".data", 6);
		}
		break;
	case 6://.section
		k = 0;
		while ((line[i] != ',') && (line[i] != '\0') && (line[i] != ' ')) {
			arg.label[k++] = line[i++];
		}
		arg.label[k] = '\0';
		int f = checkLabel();
		if (f == -1) {
			printf("Error invalid label name !  Error line: %d \n" , cntLine);
			error = 1;
			return;
		}

		if (line[i] == ' ') {
			i++;
		}
		if (line[i] == ',') {
			i++;
		}
		if (line[i] == ' ') {
			i++;
		}


		int atr = 0;
		// ako ima atributa
		if (line[i] != '\0') {
			if (line[i] == '"') {
				i++;
			}
			else {
				printf("Error: Wrong attributes format! Line Error: %d\n", cntLine);
				error = 1;
				return;
			}
			
			k = 0;
			while ((line[i] != '"') && (line[i] != '\0')) {
				if ((line[i] != 'x') && (line[i] != 'X') && (line[i] != 'r') && (line[i] != 'R') && (line[i] != 'w') && (line[i] != 'W')) {
					printf("Error: Wrong attributes! Line Error: %d\n", cntLine);
					error = 1;
					return;
				}

				if ((line[i] == 'x') || (line[i] == 'X')) {
					atr = atr | 1;
				}
				if ((line[i] == 'w') || (line[i] == 'W')) {
					atr = atr | 2;
				}
				if ((line[i] == 'r') || (line[i] == 'R')) {
					atr = atr | 4;
				}
				i++;

			}

			if (line[i] == '"') {
				i++;
			}
			else {
				printf("Error: Wrong attributes format! Line Error: %d\n", cntLine);
				error = 1;
				return;
			}

			if (line[i] != '\0') {
				printf("Error: Wrong attributes format! Line Error: %d\n", cntLine);
				error = 1;
				return;
			}
		}
		if (pass == 2) {
			processSection2(arg.label);
		}
		else {
			symTable[mySection].size = LC;
			processSection(arg.label, atr);
		}
		
		break;

	case 7://.byte
		if ((mySection == 0)||(!strcmp(symTable[mySection].name,".bss"))) {
			printf("Error:Undefined Section! Line Error: %d\n", cntLine);
			error = 1;
			return;
		}
		while (line[i] != '\0') {
			k = 0;
			while ((line[i] != ',') && (line[i] != '\0') && (line[i] != ' ')) {
				num[k++] = line[i++];
			}
			num[k] = '\0';
			k=isNumber(num, &broj);
			if (k == -1) {
				printf("Not Number! Line Error: %d\n", cntLine);
				error = 1;
				return;
			}
			if ((broj < -128) || (broj > 127)) {
				printf("Error: Number out of frame! Line Error: %d\n", cntLine);
				error = 1;
				return;
			}
			if (pass == 2) {
				binaryContent[myPosition][LC] = broj;
			}
			LC++;
			if (line[i] == ' ') {
				i++;
			}
			if (line[i] == ',') {
				i++;
				if (line[i] == ' ') {
					i++;
				}
			}
		}
		break;
	case 8://.word
		if ((mySection == 0) || (!strcmp(symTable[mySection].name, ".bss"))) {
			printf("Error:Undefined Section! Line Error: %d\n", cntLine);
			error = 1;
			return;
		}
		while (line[i] != '\0') {
			k = 0;
			while ((line[i] != ',') && (line[i] != '\0') && (line[i] != ' ')) {
				num[k++] = line[i++];
			}
			num[k] = '\0';

			strcpy(arg.label, num);
			if ((isNumber(num, &broj) == -1) && (checkLabel() == -1)){
				printf("Not number not label!  Error line: %d \n", cntLine);
				error = 1;
				return;
			}

			k = isNumber(num, &broj);
			if (k == 1) {

				if ((broj < -32768) || (broj > 32767)) {
					printf("Error: Number out of frame! Line Error: %d\n", cntLine);
					error = 1;
					return;
				}
				if (pass == 2) {
					storeWord(broj);
				}
			}
			else {
				k = checkLabel();
				if (k == 1) {
		
					int i = 0;
					for (i = 0; i < tailSymTable; i++) {
						if (!strcmp(arg.label, symTable[i].name)) {
							break;
						}
					}
					if (pass == 2) {
						relEntry *novi = malloc(sizeof(relEntry));
						novi->type = 'A';
						novi->offset = LC;
						novi->next = NULL;
						if (relHeads[myPosition] == NULL) {
							relHeads[myPosition] = novi;
						}
						else {
							relTails[myPosition]->next = novi;
						}

						relTails[myPosition] = novi;

						if ((symTable[i].local == 0) || (symTable[i].local == -1)) { //global ili extern
							novi->value = symTable[i].rbr;
							storeWord(0);
						}
						else {
							novi->value = symTable[i].section;
							storeWord(symTable[i].value);
						}
					}
				}
			}
			LC=LC+2;
			if (line[i] == ' ') {
				i++;
			}
			if (line[i] == ',') {
				i++;
				if (line[i] == ' ') {
					i++;
				}
			}
		}
		break;
	case 9://.align
		if (mySection == 0) {
			printf("Error:Undefined Section! Line Error: %d\n", cntLine);
			error = 1;
			return;
		}
		k = 0;
		while (line[i] != '\0') {
			num[k++] = line[i++];
		}
		num[k] = '\0';

		if (num[0] == '-') {
			printf("Error: Align parametar can't be negative! Line Error: %d\n", cntLine);
			error = 1;
			return;
		}

		k = isNumber(num, &broj);
		if (k == -1) {
			printf("Must be number! Line Error: %d\n", cntLine);
			error = 1;
			return;
		}
	
		if (broj < 0) {
			printf("Error:  Align parametar must be positive! Line Error: %d\n", cntLine);
			error = 1;
			return;
		}

		k = broj&(broj - 1);
		if (k != 0) {
			printf("Error:  Align parametar must be power of 2! Line Error: %d\n", cntLine);
			error = 1;
			return;
		}

		while ((LC%broj) != 0) {
			LC++;
		}

		break;
	case 10://skip
		if (mySection == 0) {
			printf("Error:Undefined Section! Line Error: %d\n", cntLine);
			error = 1;
			return;
		}
		k = 0;
		while (line[i] != '\0') {
			num[k++] = line[i++];
		}
		num[k] = '\0';

		if (num[0] == '-') {
			printf("Error: Skip parametar can't be negative! Line Error: %d\n", cntLine);
			error = 1;
			return;
		}

		k = isNumber(num, &broj);
		if (k == -1) {

			printf("must be number! Line Error: %d\n", cntLine);
			error = 1;
			return;
		}

		if (broj < 0) {
			printf("Error:  Skip parametar must be positive! Line Error: %d\n", cntLine);
			error = 1;
			return;
		}

		if (pass == 2) {
			int num = 0;
			while (num < broj) {
				binaryContent[myPosition][LC + num] = 0;
				num++;
			}
		}
		LC = LC + broj;
		break;

	case 11://end
		end = 1;
		symTable[mySection].size = LC;
		break;
	}

}



void processLabel() {
	int i = 0,r=0;

	if (pass == 2) {
		return;
	}

	if (mySection == 0) {
		printf("Error:Undefined Section! Line Error: %d\n", cntLine);
		error = 1;
		return;
	}

	if (arg.label[0] == '\0') {
		printf("Error: label name is empty! Line Error: %d\n", cntLine);
		error = 1;
		return;
	}


	r = checkLabel();
	if (r == -1) {
		printf("Error invalid label name !  Error line: %d \n" , cntLine);
		error = 1;
		return;
	}
	while (i < tailSymTable) {
		if (!strcmp(symTable[i].name, arg.label)) {
			if (symTable[i].section>0){
				printf("Error: label already exists! Line error:  %d\n", cntLine);
				error = 1;
				return;
			}
			else {
				if (symTable[i].section == -1) {
					printf("Error: label already exists! Line error: %d\n", cntLine);
					error = 1;
					return;
				}
				else {
					symTable[i].section = symTable[mySection].rbr;
					symTable[i].value = LC;
					return;
				}
			}
			
		}
		i++;
	}

	if (i == tailSymTable) {
		strcpy(symTable[tailSymTable].name, arg.label);
		symTable[tailSymTable].local = 1;
		symTable[tailSymTable].rbr = tailSymTable;
		symTable[tailSymTable].section = symTable[mySection].rbr;
		symTable[tailSymTable].value = LC;
		symTable[tailSymTable].attributes = 0;
		symTable[tailSymTable].position = -1;
		symTable[tailSymTable].size = 0;
		tailSymTable++;
		if (tailSymTable == Kap) {
			symTable = realloc(symTable, Kap * 2 * sizeof(symTabEntry));
			Kap = Kap * 2;
		}
	}

}

int indexOpcode(char *dir) {
	int i = 0;
	for (i = 0; i < numOpcodes; i++) {
		if (!strcmp(opcode[i], dir)) {
			return i;
		}
	}
	return -1;
}

void processOperand(char *operand,int d) {
	int br;

	//<val>
	if ((isNumber(operand, &br))!=-1) {
		if (d == 1 && instrDescr.part.oc != 9 && instrDescr.part.oc != 3) {
			printf("Invalid Operand! Line num: %d \n", cntLine);
			error = 1;
			return;
		}

		if (instrDescr.part.oc == 2) {
			printf("Invalid Operand! Line num: %d \n", cntLine);
			error = 1;
			return;
		}

		if (bw == 'b') {
			if (br < -128 || br>255) {
				printf("Invalid Operand! Line num: %d \n", cntLine);
				error = 1;
				return;
			}

			if (pass == 1) {
				LC = LC + 2;
				return;
			}
			if (pass == 2) {
				binaryContent[myPosition][LC++] = 0;
				binaryContent[myPosition][LC++] = br;
				return;
			}
		}
		else {
			if (br < -32768 || br>65535) {
				printf("Invalid Operand! Line num: %d \n", cntLine);
				error = 1;
				return;
			}

			if (pass == 1) {
				LC = LC + 3;
				return;
			}

			if (pass == 2) {
				binaryContent[myPosition][LC++] = 0;
				storeWord(br);
				LC = LC + 2;
				return;
			}
		}

		return;
	}

	//<&symbol_name>
	if (operand[0] == '&') {
		if (bw != 'w') {
			printf("Invalid Opcode! Line num: %d \n", cntLine);
			error = 1;
			return;
		}

		if (d == 1) {
			printf("Invalid Operand! Line num: %d \n", cntLine);
			error = 1;
			return;
		}

		strcpy(arg.label, operand+1);
		int f=checkLabel();
		if (f == -1) {
			printf("Error invalid label name !  Error line: %d \n", cntLine);
			error = 1;
			return;
		}

		if (pass == 1) {
			LC = LC + 3;
		}


		if (pass == 2) {
			int i = 0;

			binaryContent[myPosition][LC++] = 0;

			for (i = 0; i < tailSymTable; i++) {
				if (!strcmp(arg.label, symTable[i].name)) {
					break;
				}
			}
			if (i == tailSymTable) {
				printf("Label doesn't exist! Line num: %d \n", cntLine);
				error = 1;
				return;
			}

			// .equ
			if (symTable[i].section == -1) {
				printf(".equ symbol doesn't have address! Line num: %d \n", cntLine);
				error = 1;
				return;
			}

			relEntry *novi = malloc(sizeof(relEntry));
			novi->type = 'A';
			novi->offset = LC;
			novi->next = NULL;
			if (relHeads[myPosition] == NULL) {
				relHeads[myPosition] = novi;
			}
			else {
				relTails[myPosition]->next = novi;
			}

			relTails[myPosition] = novi;
				
			if ((symTable[i].local == 0) || (symTable[i].local == -1)) { //global ili extern
				novi->value = symTable[i].rbr;
				storeWord(0);
			}
			else {
				novi->value = symTable[i].section;
				storeWord(symTable[i].value);
			}
			LC = LC + 2;
			
			
		}
		return;
	}

	//r<num>
	if ((operand[0] == 'r') && (operand[1] >= '0' && operand[1] <= '7') && ((operand[2] == '\0')|| ((operand[2] == 'h')&&(operand[3] == '\0'))|| ((operand[2] == 'l')&&(operand[3] == '\0')))) {
		
		if (((operand[2] == 'h') || (operand[2] == 'l')) && (bw=='w')) {
			printf("Invalid Operand! Line num: %d \n", cntLine);
			error = 1;
			return;
		}


		if ((operand[2] == '\0') && (bw == 'b')) {
			printf("Invalid Operand! Line num: %d \n", cntLine);
			error = 1;
			return;
		}

		if (pass == 1) {
			LC++;
		}
		if (pass == 2) {
			OpDescr opDescr;
			opDescr.full = 0;
			opDescr.part.am = 1;
			opDescr.part.r = operand[1] - '0';
			if (operand[2] == 'h') {
				opDescr.part.lh = 1;
			}
			binaryContent[myPosition][LC++] = opDescr.full;
		}
		return;
	}

	//r<num>[val] i r<num>[symbol_name]
	int i = 0; char num[100]; 
	if ((operand[0] == 'r') && (operand[1] >= '0' && operand[1] <= '7') && (operand[2] == '[')) {
		int k = 0;
		i = 3;
		while ( (operand[i] != '\0') && (operand[i] != ']')) {
			num[k++] = operand[i++];
		}
		num[k] = '\0';
		if (operand[i] != ']') {
			printf("Invalid Operand! Line num: %d \n", cntLine);
			error = 1;
			return;
		}

		int broj;

		strcpy(arg.label, num);
		if ((isNumber(num, &broj) == -1) && (checkLabel() == -1)) {
			printf("Not number not label!  Error line: %d \n" , cntLine);
			error = 1;
			return;
		}


		OpDescr opDescr;
 
		if (pass == 2) {
			opDescr.full = 0;
			opDescr.part.r = operand[1] - '0';
		}

		int r = isNumber(num, &broj);
		if (r == 1) {
			if (broj == 0) {
				if (pass == 1) {
					LC = LC + 1;
					return;
				}
				opDescr.part.am = 2;
				binaryContent[myPosition][LC++] = opDescr.full;
			}
			else if (broj > -128 && broj < 256) {
				if (pass == 1) {
					LC = LC + 2;
					return;
				}
				opDescr.part.am = 3;
				binaryContent[myPosition][LC++] = opDescr.full;
				binaryContent[myPosition][LC++] = broj;
			}
			else {
				opDescr.part.am = 4;
				binaryContent[myPosition][LC++] = opDescr.full;
				storeWord(broj);
				LC = LC + 2;
			}


			
		}
		else {
			r = checkLabel();
			if (r == 1) {
				if (pass == 1) {
					LC = LC + 3;
					return;
				}
				opDescr.part.am = 4;
				binaryContent[myPosition][LC++] = opDescr.full;

				int i = 0;
				for (i = 0; i < tailSymTable; i++) {
					if (!strcmp(arg.label, symTable[i].name)) {
						break;
					}
				}

				if (i == tailSymTable) {
					printf("Label doesn't exist! Line num: %d \n", cntLine);
					error = 1;
					return;
				}

				// .equ
				if (symTable[i].section == -1) {
					storeWord(symTable[i].value);
				}
				else {
					relEntry *novi = malloc(sizeof(relEntry));
					novi->type = 'A';
					novi->offset = LC;
					novi->next = NULL;
					if (relHeads[myPosition] == NULL) {
						relHeads[myPosition] = novi;
					}
					else {
						relTails[myPosition]->next = novi;
					}

					relTails[myPosition] = novi;

					if ((symTable[i].local == 0) || (symTable[i].local == -1)) { //global ili extern
						novi->value = symTable[i].rbr;
						storeWord(0);
					}
					else {
						novi->value = symTable[i].section;
						storeWord(symTable[i].value);
					}
				}
				LC = LC + 2;
			}
		}

		return;
	}

	//$<symbol_name>-pc relativno adresiranje simbola <symbol_name>
	if (operand[0] == '$') {

		strcpy(arg.label, operand + 1);
		int f = checkLabel();
		if (f == -1) {
			printf("Error invalid label name !  Error line: %d \n" , cntLine);
			error = 1;
			return;
		}

		if (pass == 1) {
			LC = LC + 3;
		}

		if (pass == 2) {
			OpDescr opDescr;
			opDescr.full = 0;
			opDescr.part.am = 4;
			opDescr.part.r = 7;

			binaryContent[myPosition][LC++] = opDescr.full;

			int i = 0;
			for (i = 0; i < tailSymTable; i++) {
				if (!strcmp(arg.label, symTable[i].name)) {
					break;
				}
			}

			if (i == tailSymTable) {
				printf("Label doesn't exist! Line num: %d \n", cntLine);
				error = 1;
				return;
			}

			// .equ
			if (symTable[i].section == -1) {
				printf(".equ symbol can't bre pc relative referenced Line num: %d \n", cntLine);
				error = 1;
				return;
			}

			if (symTable[i].section != mySection){
				relEntry *novi = malloc(sizeof(relEntry));
				novi->type = 'R';
				novi->offset = LC;
				novi->next = NULL;
				if (relHeads[myPosition] == NULL) {
					relHeads[myPosition] = novi;
				}
				else {
					relTails[myPosition]->next = novi;
				}

				relTails[myPosition] = novi;

				if ((symTable[i].local == 0) || (symTable[i].local == -1)) { //global ili extern
					novi->value = symTable[i].rbr;
				}
				else {
					novi->value = symTable[i].section;

				}
				if (d == 1) {
					dstRelIndex = i;
					dstRelLC = LC;
					dstSameSec = 0;
				}
				else {
					srcRelIndex = i;
					srcRelLC = LC;
					srcSameSec = 0;
				}
			}
			else {
				if (d == 1) {
					dstRelIndex = i;
					dstRelLC = LC;
					dstSameSec = 1;
				}
				else {
					srcRelIndex = i;
					srcRelLC = LC;
					srcSameSec = 1;
				}
			}
			

			LC = LC + 2;

		}
		return;
	}

	//*<val>- apsolutno adresiranje podatka u memoriji na adresi ukazanoj vrednoscu <val>
	if (operand[0] == '*') {
		
		int broj,r;
		r=isNumber(operand + 1, &broj);
		if (r == -1) {
			printf("Must be number! Line num: %d \n", cntLine);
			error = 1;
			return;
		}

		if ((short)broj < -32768 || (short)broj > 32767) {
			printf("Invalid Operand! Line num: %d \n", cntLine);
			error = 1;
			return;
		}

		if (pass == 1) {
			LC = LC + 3;
		}

		if (pass == 2) {
			OpDescr opDescr;
			opDescr.full = 0;
			opDescr.part.am = 5;
			
			binaryContent[myPosition][LC++] = opDescr.full;
			storeWord(broj);
			LC = LC + 2;

		}
		return;
	}

	//psw
	if (!strcmp(operand, "psw")|| !strcmp(operand, "pswh")|| !strcmp(operand, "pswl")) {
		if (((operand[3] == 'h') || (operand[3] == 'l')) && (bw == 'w')) {
			printf("Invalid Operand! Line num: %d \n", cntLine);
			error = 1;
			return;
		}


		if ((operand[3] == '\0') && (bw == 'b')) {
			printf("Invalid Operand! Line num: %d \n", cntLine);
			error = 1;
			return;
		}
		if (pass == 1) {
			LC++;
			return;
		}

		if (pass == 2) {
			OpDescr opDescr;
			opDescr.full = 0;
			opDescr.part.am = 1;
			opDescr.part.r = 0xF;
			if (operand[3] == 'h') {
				opDescr.part.lh = 1;
			}
			binaryContent[myPosition][LC++] = opDescr.full;
		}

		return;
	}

	//sp
	if (!strcmp(operand, "sp") || !strcmp(operand, "sph") || !strcmp(operand, "spl")) {
		if (((operand[2] == 'h') || (operand[2] == 'l')) && (bw == 'w')) {
			printf("Invalid Operand! Line num: %d \n", cntLine);
			error = 1;
			return;
		}


		if ((operand[2] == '\0') && (bw == 'b')) {
			printf("Invalid Operand! Line num: %d \n", cntLine);
			error = 1;
			return;
		}
		if (pass == 1) {
			LC++;
			return;
		}

		if (pass == 2) {
			OpDescr opDescr;
			opDescr.full = 0;
			opDescr.part.am = 1;
			opDescr.part.r = 6;
			if (operand[2] == 'h') {
				opDescr.part.lh = 1;
			}
			binaryContent[myPosition][LC++] = opDescr.full;
		}

		return;
	}

	//pc
	if (!strcmp(operand, "pc") || !strcmp(operand, "pch") || !strcmp(operand, "pcl")) {
		if (((operand[2] == 'h') || (operand[2] == 'l')) && (bw == 'w')) {
			printf("Invalid Operand! Line num: %d \n", cntLine);
			error = 1;
			return;
		}


		if ((operand[2] == '\0') && (bw == 'b')) {
			printf("Invalid Operand! Line num: %d \n", cntLine);
			error = 1;
			return;
		}
		if (pass == 1) {
			LC++;
			return;
		}

		if (pass == 2) {
			OpDescr opDescr;
			opDescr.full = 0;
			opDescr.part.am = 1;
			opDescr.part.r = 7;
			if (operand[2] == 'h') {
				opDescr.part.lh = 1;
			}
			binaryContent[myPosition][LC++] = opDescr.full;
		}

		return;
	}

	//<symbol_name>-apsolutno adresiranje simbola <symbol_name>
	strcpy(arg.label, operand);
	int r = checkLabel();
	if (r == -1) {
		printf("Error invalid label name !  Error line: %d \n" , cntLine);
		error = 1;
		return;
	}
	if (pass == 1) {
		LC = LC + 3;
		return;
	}

	i = 0;
	
	OpDescr opDescr;
	opDescr.full = 0;
	
	if (instrDescr.part.oc>=19 && instrDescr.part.oc<=23){
		opDescr.part.am=0;
	}

	else{
		opDescr.part.am = 5;
	}

	
	int symsec = -1;
	for (i = 0; i < tailSymTable; i++) {
		if (!strcmp(arg.label, symTable[i].name)) {
			symsec = symTable[i].section;
			break;
		}
	}

	if (i == tailSymTable) {
		printf("Label doesn't exist! Line num: %d \n", cntLine);
		error = 1;
		return;
	}

	//.equ
	if (symsec == -1) {
		opDescr.part.am = 0;
		binaryContent[myPosition][LC++] = opDescr.full;

		storeWord(symTable[i].value);
	}
	else {
		binaryContent[myPosition][LC++] = opDescr.full;

		relEntry *novi = malloc(sizeof(relEntry));
		novi->type = 'A';
		novi->offset = LC;
		novi->next = NULL;
		if (relHeads[myPosition] == NULL) {
			relHeads[myPosition] = novi;
		}
		else {
			relTails[myPosition]->next = novi;
		}

		relTails[myPosition] = novi;

		if ((symTable[i].local == 0) || (symTable[i].local == -1)) { //global ili extern
			novi->value = symTable[i].rbr;
			storeWord(0);
		}
		else {
			novi->value = symTable[i].section;
			storeWord(symTable[i].value);
		}
	}
	LC = LC + 2;

	
}

void processOperationCode() {

	if ((mySection == 0) || (!strcmp(symTable[mySection].name, ".bss"))) {
		printf("Error:Undefined Section! Line Error: %d\n", cntLine);
		error = 1;
		return;
	}
	
	int i = 0;
	while (arg.operation[i] != '\0') {
		i++;
	}
	bw = arg.operation[i - 1];
	if (bw == 'b' || bw == 'w') {
		arg.operation[i - 1] = '\0';
	}


	int s = indexOpcode(arg.operation);

	if (s == -1) {
		printf("Invalid Operation! Line num: %d \n",cntLine);
		error = 1;
		return;
	}

	instrDescr.full = 0;
	srcRelIndex = -1;
	dstRelIndex = -1;

	switch(s) {
	case 0://halt
		if (pass == 2) {
			instrDescr.part.oc = 1;
			binaryContent[myPosition][LC] = instrDescr.full;
		}
		LC++;
		break;
	case 1://xchg
		if ((bw != 'b') && (bw != 'w')) {
			printf("Invalid Operation! Line num: %d \n", cntLine);
			error = 1;
			return;
		}
		if (pass == 2) {
			instrDescr.part.oc = 2;
			if (bw == 'b') {
				instrDescr.part.s = 0;
			}
			if (bw == 'w') {
				instrDescr.part.s = 1;
			}
			binaryContent[myPosition][LC] = instrDescr.full;
		}
		LC++;
		processOperand(arg.dst,1);
		processOperand(arg.src,0);
		break;
	case 2://int
		if ((bw != 'b') && (bw != 'w')) {
			printf("Invalid Operation! Line num: %d \n", cntLine);
			error = 1;
			return;
		}
		instrDescr.part.oc = 3;
		if (pass == 2) {
			instrDescr.part.oc = 3;
			if (bw == 'b') {
				instrDescr.part.s = 0;
			}
			if (bw == 'w') {
				instrDescr.part.s = 1;
			}
			binaryContent[myPosition][LC] = instrDescr.full;
		}
		LC++;
		processOperand(arg.dst, 1);
		break;
	case 3://mov
		if ((bw != 'b') && (bw != 'w')) {
			printf("Invalid Operation! Line num: %d \n", cntLine);
			error = 1;
			return;
		}
		if (pass == 2) {
			instrDescr.part.oc = 4;
			if (bw == 'b') {
				instrDescr.part.s = 0;
			}
			if (bw == 'w') {
				instrDescr.part.s = 1;
			}
			binaryContent[myPosition][LC] = instrDescr.full;
		}
		LC++;
		processOperand(arg.dst, 1);
		processOperand(arg.src, 0);
		break;
	case 4://add
		if ((bw != 'b') && (bw != 'w')) {
			printf("Invalid Operation! Line num: %d \n", cntLine);
			error = 1;
			return;
		}
		if (pass == 2) {
			instrDescr.part.oc = 5;
			if (bw == 'b') {
				instrDescr.part.s = 0;
			}
			if (bw == 'w') {
				instrDescr.part.s = 1;
			}
			binaryContent[myPosition][LC] = instrDescr.full;
		}
		LC++;
		processOperand(arg.dst, 1);
		processOperand(arg.src, 0);
		break;
	case 5://sub
		if ((bw != 'b') && (bw != 'w')) {
			printf("Invalid Operation! Line num: %d \n", cntLine);
			error = 1;
			return;
		}
		if (pass == 2) {
			instrDescr.part.oc = 6;
			if (bw == 'b') {
				instrDescr.part.s = 0;
			}
			if (bw == 'w') {
				instrDescr.part.s = 1;
			}
			binaryContent[myPosition][LC] = instrDescr.full;
		}
		LC++;
		processOperand(arg.dst, 1);
		processOperand(arg.src, 0);
		break;
	case 6://mul
		if ((bw != 'b') && (bw != 'w')) {
			printf("Invalid Operation! Line num: %d \n", cntLine);
			error = 1;
			return;
		}
		if (pass == 2) {
			instrDescr.part.oc = 7;
			if (bw == 'b') {
				instrDescr.part.s = 0;
			}
			if (bw == 'w') {
				instrDescr.part.s = 1;
			}
			binaryContent[myPosition][LC] = instrDescr.full;
		}
		LC++;
		processOperand(arg.dst, 1);
		processOperand(arg.src, 0);
		break;
	case 7://div
		if ((bw != 'b') && (bw != 'w')) {
			printf("Invalid Operation! Line num: %d \n", cntLine);
			error = 1;
			return;
		}
		if (pass == 2) {
			instrDescr.part.oc = s+1;
			if (bw == 'b') {
				instrDescr.part.s = 0;
			}
			if (bw == 'w') {
				instrDescr.part.s = 1;
			}
			binaryContent[myPosition][LC] = instrDescr.full;
		}
		LC++;
		processOperand(arg.dst, 1);
		processOperand(arg.src, 0);
		break;
	case 8://cmp
		if ((bw != 'b') && (bw != 'w')) {
			printf("Invalid Operation! Line num: %d \n", cntLine);
			error = 1;
			return;
		}
		instrDescr.part.oc = s + 1;
		if (pass == 2) {
			instrDescr.part.oc = s + 1;
			if (bw == 'b') {
				instrDescr.part.s = 0;
			}
			if (bw == 'w') {
				instrDescr.part.s = 1;
			}
			binaryContent[myPosition][LC] = instrDescr.full;
		}
		LC++;
		processOperand(arg.dst, 1);
		processOperand(arg.src, 0);
		break;
	case 9://not
		if ((bw != 'b') && (bw != 'w')) {
			printf("Invalid Operation! Line num: %d \n", cntLine);
			error = 1;
			return;
		}
		if (pass == 2) {
			instrDescr.part.oc = s + 1;
			if (bw == 'b') {
				instrDescr.part.s = 0;
			}
			if (bw == 'w') {
				instrDescr.part.s = 1;
			}
			binaryContent[myPosition][LC] = instrDescr.full;
		}
		LC++;
		processOperand(arg.dst, 1);
		break;
	case 10://and
		if ((bw != 'b') && (bw != 'w')) {
			printf("Invalid Operation! Line num: %d \n", cntLine);
			error = 1;
			return;
		}
		if (pass == 2) {
			instrDescr.part.oc = s + 1;
			if (bw == 'b') {
				instrDescr.part.s = 0;
			}
			if (bw == 'w') {
				instrDescr.part.s = 1;
			}
			binaryContent[myPosition][LC] = instrDescr.full;
		}
		LC++;
		processOperand(arg.dst, 1);
		processOperand(arg.src, 0);
		break;
	case 11://or
		if ((bw != 'b') && (bw != 'w')) {
			printf("Invalid Operation! Line num: %d \n", cntLine);
			error = 1;
			return;
		}
		if (pass == 2) {
			instrDescr.part.oc = s + 1;
			if (bw == 'b') {
				instrDescr.part.s = 0;
			}
			if (bw == 'w') {
				instrDescr.part.s = 1;
			}
			binaryContent[myPosition][LC] = instrDescr.full;
		}
		LC++;
		processOperand(arg.dst, 1);
		processOperand(arg.src, 0);
		break;
	case 12://xor
		if ((bw != 'b') && (bw != 'w')) {
			printf("Invalid Operation! Line num: %d \n", cntLine);
			error = 1;
			return;
		}
		if (pass == 2) {
			instrDescr.part.oc = s + 1;
			if (bw == 'b') {
				instrDescr.part.s = 0;
			}
			if (bw == 'w') {
				instrDescr.part.s = 1;
			}
			binaryContent[myPosition][LC] = instrDescr.full;
		}
		LC++;
		processOperand(arg.dst, 1);
		processOperand(arg.src, 0);
		break;
	case 13://test
		if ((bw != 'b') && (bw != 'w')) {
			printf("Invalid Operation! Line num: %d \n", cntLine);
			error = 1;
			return;
		}
		if (pass == 2) {
			instrDescr.part.oc = s + 1;
			if (bw == 'b') {
				instrDescr.part.s = 0;
			}
			if (bw == 'w') {
				instrDescr.part.s = 1;
			}
			binaryContent[myPosition][LC] = instrDescr.full;
		}
		LC++;

		processOperand(arg.dst, 1);
		processOperand(arg.src, 0);
		break;
	case 14://shl
		if ((bw != 'b') && (bw != 'w')) {
			printf("Invalid Operation! Line num: %d \n", cntLine);
			error = 1;
			return;
		}
		if (pass == 2) {
			instrDescr.part.oc = s + 1;
			if (bw == 'b') {
				instrDescr.part.s = 0;
			}
			if (bw == 'w') {
				instrDescr.part.s = 1;
			}
			binaryContent[myPosition][LC] = instrDescr.full;
		}
		LC++;
		processOperand(arg.dst, 1);
		processOperand(arg.src, 0);
		break;
	case 15://shr
		if ((bw != 'b') && (bw != 'w')) {
			printf("Invalid Operation! Line num: %d \n", cntLine);
			error = 1;
			return;
		}
		if (pass == 2) {
			instrDescr.part.oc = s + 1;
			if (bw == 'b') {
				instrDescr.part.s = 0;
			}
			if (bw == 'w') {
				instrDescr.part.s = 1;
			}
			binaryContent[myPosition][LC] = instrDescr.full;
		}
		LC++;
		processOperand(arg.dst, 1);
		processOperand(arg.src, 0);
		break;
	case 16://push
		if (pass == 2) {
			instrDescr.part.oc = s + 1;
			instrDescr.part.s = 1;
			
			binaryContent[myPosition][LC] = instrDescr.full;
		}
		LC++;
		strcpy(arg.src, arg.dst);
		processOperand(arg.src, 0);
		break;
	case 17://pop
		if (pass == 2) {
			instrDescr.part.oc = s + 1;
			instrDescr.part.s = 1;

			binaryContent[myPosition][LC] = instrDescr.full;
		}
		LC++;
		processOperand(arg.dst, 1);
		break;
	case 18://jmp
		if (pass == 2) {
			instrDescr.part.oc = s + 1;
			instrDescr.part.s = 1;
			binaryContent[myPosition][LC] = instrDescr.full;
		}
		LC++;
		processOperand(arg.dst, 1);
		break;
	case 19://jeq
		if (pass == 2) {
			instrDescr.part.oc = s + 1;
			instrDescr.part.s = 1;
			binaryContent[myPosition][LC] = instrDescr.full;
		}
		LC++;
		processOperand(arg.dst, 1);
		break;
	case 20://jne
		if (pass == 2) {
			instrDescr.part.oc = s + 1;
			instrDescr.part.s = 1;
			binaryContent[myPosition][LC] = instrDescr.full;
		}
		LC++;
		processOperand(arg.dst, 1);
		break;
	case 21://jgt
		if (pass == 2) {
			instrDescr.part.oc = s + 1;
			instrDescr.part.s = 1;
			binaryContent[myPosition][LC] = instrDescr.full;
		}
		LC++;
		processOperand(arg.dst, 1);
		break;
	case 22://call
		if (pass == 2) {
			instrDescr.part.oc = s + 1;
			instrDescr.part.s = 1;
			binaryContent[myPosition][LC] = instrDescr.full;
		}
		LC++;
		processOperand(arg.dst, 1);
		break;
	case 23://ret
		if (pass == 2) {
			instrDescr.part.oc = s + 1;

			binaryContent[myPosition][LC] = instrDescr.full;
		}
		LC++;
		break;
	case 24://iret
		if (pass == 2) {
			instrDescr.part.oc = s + 1;

			binaryContent[myPosition][LC] = instrDescr.full;
		}
		LC++;
		break;

	}

	if (srcRelIndex != -1) {
		int br = LC - srcRelLC;
		br = 2;
		int rez;
		
		if (symTable[srcRelIndex].local == 1) { //local
			if (srcSameSec == 1) {
				br = srcRelLC + 2;;
			}
			rez = symTable[srcRelIndex].value - br;
		}
		else {
			rez = 0 - br;
		}

		int mask = 0xFF;
		char lower, higher;
		lower = rez&mask;
		rez = rez >> 8;
		higher = rez&mask;
		binaryContent[myPosition][srcRelLC] = lower;
		binaryContent[myPosition][srcRelLC + 1] = higher;
	}

	if (dstRelIndex != -1) {
		int br = LC - dstRelLC;
		br = 2;
		int rez;

		
		if (symTable[dstRelIndex].local == 1) { //local
			if (dstSameSec == 1) {
				br = dstRelLC + 2;;
			}
			rez = symTable[dstRelIndex].value - br;
		}
		else {
			rez = 0 - br;
		}
		
		int mask = 0xFF;
		char lower, higher;
		lower = rez&mask;
		rez = rez >> 8;
		higher = rez&mask;
		binaryContent[myPosition][dstRelLC] = lower;
		binaryContent[myPosition][dstRelLC + 1] = higher;
	}
	
}

	


void processingArgs() {

	if (arg.label[0] != '\0') {
		processLabel();
	}

	if (arg.operation[0] == '\0') {
		return;
	}
	
	processOperationCode();


}

/*typedef union test
{
	struct T {
		unsigned char x : 2;
		unsigned char y : 2;
		unsigned char z : 2;
	} t;
	unsigned char full;
} Test;
*/

void main(int argc, char* argv[]) {

	//Test t;

	//t.full = 0;
	//t.t.y = 3;

	//printf("%02hhX\n", t.full);
	//system("pause");
	//return;
	char *in, *out;
	in = NULL;
	out = NULL;
	cntLine = 0;
	error = 0;
	mySection = 0;

	
	
	if (argc != 4) {
		printf("Invalid arguments number! \n");
		error = 1;
		system("pause");
		exit(-1);
	}
	if (!strcmp(argv[1], "-o")) {
		in = argv[3];
		out = argv[2];
	}
	else {
		if (!strcmp(argv[2], "-o")) {
			in = argv[1];
			out = argv[3];
		}
		else {
			printf("Arguments Error! \n");
			error = 1;
			system("pause");
			exit(-1);
		}
	}

	inFile = fopen(in, "r");

	if (inFile == NULL) {
		printf("Invalid input file name! \n");
		error = 1;
		system("pause");
		exit(-1);
	}

	symTable = malloc(sizeof(symTabEntry)*Kap);

	tailSymTable = 0;

	strcpy(symTable[tailSymTable].name, "UND");
	symTable[tailSymTable].local = 1;
	symTable[tailSymTable].attributes = 0;
	symTable[tailSymTable].position = -1;
	symTable[tailSymTable].rbr = 0;
	symTable[tailSymTable].section = -1;
	symTable[tailSymTable].size = 0;
	symTable[tailSymTable].value=0;

	tailSymTable++;


	c = fgetc(inFile);
	pass = 1;
	cntLine = 1;

	while (c != EOF) {
		arg.label[0] = '\0';
		arg.operation[0] = '\0';
		arg.dst[0] = '\0';
		arg.src[0] = '\0';
		readLine();
	
		if (line[0] == '.') {
			processingDirective();
		}
		else{
			extractArgs();
			processingArgs();
		}
		cntLine++;
		if (end == 1) {
			break;
		}
		c = fgetc(inFile);
	}

	fclose(inFile);

	int m = 0;
	while (m < tailSymTable) {
		if (symTable[m].local == 0 && symTable[m].section == 0) {
			error = 1;
			break;
		}

		if (symTable[m].local == -1 && symTable[m].section != 0) {
			error = 1;
			break;
		}

		if (symTable[m].local == -1) {
			symTable[m].local = 0;
		}

		m++;
	}

	if (error == 1) {
		printf("Process Failed! \n");
		system("pause");
		exit(-1);
	}

	inFile = fopen(in, "r");

	relHeads = malloc(sizeof(relEntry*)*kapRels);
	relTails = malloc(sizeof(relEntry*)*kapRels);
	binaryContent = malloc(sizeof(char*)*kapRels);

	c = fgetc(inFile);
	pass = 2;
	end = 0;
	LC = 0;
	cntLine = 1;

	while (c != EOF) {
		arg.label[0] = '\0';
		arg.operation[0] = '\0';
		arg.dst[0] = '\0';
		arg.src[0] = '\0';
		readLine();

		if (line[0] == '.') {
			processingDirective();
		}
		else {
			extractArgs();
			processingArgs();
		}
		cntLine++;
		if (end == 1) {
			break;
		}
		c = fgetc(inFile);
	}

	if (error == 1) {
		printf("Process Failed! \n");
		system("pause");
		exit(-1);
	}

	fclose(inFile);


	outFile = fopen(out, "w");

	int i = 0;

	fprintf(outFile, "%d\n", tailSymTable);
	
	while (i < tailSymTable) {
		fprintf(outFile, "%d %s %d %d %d %d %d %d\n", symTable[i].rbr, symTable[i].name, symTable[i].section, symTable[i].value, symTable[i].local, symTable[i].size, symTable[i].attributes, symTable[i].position);
		i++;
	}
	
	//fprintf(outFile, "%d\n", tailRels);

	i = 0;
	int cnt = 0;;
	m = 0;
	relEntry *tek;
	while (i < tailRels) {
		m = 0; cnt = 0;
		while (m < tailSymTable) {
			if (symTable[m].position == i) {
				break;
			}
			m++;
		}

		tek = relHeads[i];

		while (tek != NULL) {
			cnt++;
			tek = tek->next;
		}

		fprintf(outFile, "%s\n", symTable[m].name);
		fprintf(outFile, "%d\n", cnt);

		tek = relHeads[i];

		while(tek!=NULL){
			fprintf(outFile, "%d %c %d\n", tek->offset, tek->type, tek->value);
			tek = tek->next;
		}
		
		int j = 0;
	
		while (j < symTable[m].size) {
			fprintf(outFile, "%02hhX" ,binaryContent[i][j]);
			j++;
		}
		
		fprintf(outFile, "\n");
		i++;

	}

	fclose(outFile);

	printf("Assembler finished succesfully\n");
}



