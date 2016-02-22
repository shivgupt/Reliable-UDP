#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <iostream>
#include <fstream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <math.h>
#include <cstdlib>
#include <pthread.h>


int server_fd;
char data[1222]; // To read request from client
sockaddr_in saddr,caddr;
int fp = 0;          // Pointer to last byte read from the file
unsigned int ack,seq;
const unsigned int packetsize = 1222;       // MSS
bool sending,receiving;
int sn=0,sf=0;
unsigned int sfseq;      // sender's last and sender's first pointer sequence no. of first
int rwnd;
int *ackarray;
unsigned int cwnd = 1;           //1 MSS
unsigned int swnd;               // min(rwnd,cwnd)
int ssthresh = 64;
char *file_name;
int dupAck=0;
int phase=-1;      //0=fast recovery, -1=slow start, 1=congestion avoidance
int tcnt=1;

using namespace std;

struct RUDPHeader
{
	char SEQ[10];
	char ACK[10];
	char F;		//fin flag
	char A;		//ack flag

};

struct RUDPpacket
{
	RUDPHeader H;
	char data[packetsize-22];
}req,res;

unsigned int atoi(char a[])
{
    int len = strlen(a);
    int i=0,result=0;
    while(i < len )
    {
	result = result + ( pow(10,len-i-1)*((int)(a[i]-48)));
	i++;
    }
    return result;
}

void itoa(unsigned int a,char b[])
{
        char t[10];
        int i=0,k;
        while(a != 0)
        {
		t[i++] = a%10 + 48;
                a /= 10;
        }
        if(i<10)
            for(k=0; k < 10-i; k++)
                b[k] = '0';

        while(i>0)
        {
       		 b[k++] = t[i-1];
                 i--;
        }
}

void shiftWindow(int k)
{
    int i;

    for(i=0;i<swnd-k;i++)
        ackarray[i] = ackarray[i+k];

    for(i;i<swnd;i++)
        ackarray[i] = 0;

    sf+=k;
    sfseq = sfseq +((packetsize+1)*k);
}

void parseRequest()
{
    int i,k=0;
    char temp[10];
    for(i=0; i < 10; i++)
    {
        req.H.SEQ[i] = data[k++];
    }

    for(i=0; i < 10; i++)
    {
        req.H.ACK[i] = data[k];
        temp[i] = data[k++];
    }
    temp[i]='\0';

    req.H.F = data[k++];
    req.H.A = data[k++];
    i=0;
    bzero(req.data,packetsize-22);
    while(k < strlen(data))
    {
       req.data[i++] = data[k++];
    }

    if ( req.H.F == '1')
        receiving = false;

    if ( req.H.A == '1')
        ack = atoi(temp);

 //   seq = atoi(req.H.SEQ);
}

void createResponse(char buffer[],char file[])
{
    int i,k=0,j=0;
    char ch;
    itoa(seq+packetsize+1,res.H.SEQ);
    seq += packetsize+1;
    for (i=0;i<10;i++)
        res.H.ACK[i] = '0';

    res.H.A = '0';
    fstream ifile;
    ifile.open(file,ios::in);
    if (ifile)
    {
            ifile.unsetf(ios_base::skipws);
            ifile.seekg(fp);
            while(ifile && k < packetsize-22)
            {

                ifile >> res.data[k++];
            }
            fp += k;
            if(ifile)
                res.H.F = '0';
            else
            {
                res.H.F = '1';
                sending = false;
                fp=0;
            }
            ifile.close();
    }
    else
    {
        char r[] = "Not Found";
        for(k;k<strlen(r);k++)
            res.data[k]=r[k];
        res.H.F = '1';
        sending = false;
        fp=0;
    }

    bzero(buffer,packetsize);
    for(i=0;i<10;i++)
        buffer[j++] = res.H.SEQ[i];
    for(i=0;i<10;i++)
        buffer[j++] = res.H.ACK[i];
    buffer[j++] = res.H.F;
    buffer[j++] = res.H.A;
    for(i = 0; i<k; i++)
        buffer[j++] = res.data[i];
}

void createSocket(char a[])
{
    server_fd = socket(AF_INET,SOCK_DGRAM,0);

    if (server_fd < 0)
    {
        cout << "Invalid Socket";
    }
    unsigned int port = atoi(a);
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(port);
    saddr.sin_addr.s_addr = INADDR_ANY;

    if(bind(server_fd,(sockaddr *)&saddr,sizeof(saddr))<0)
    {
        cout << "Could not bind!!";
    }

}

void createDupPack(char buffer[],int pseq)
{
        RUDPpacket dupPack;
        char seq[10];
        int i,j=0;
        int fd = ((pseq-sfseq)/packetsize+1)*sizeof(res.data);
        itoa(pseq,seq);
        memcpy(dupPack.H.SEQ,seq,10);
        bzero(dupPack.H.ACK,sizeof(dupPack.H.ACK));
        dupPack.H.A = '0';
        fstream ifile;int k =0;
        ifile.open(file_name,ios::in);
        if (ifile)
        {
            ifile.unsetf(ios_base::skipws);
            ifile.seekg(fd);
            while(ifile && k < packetsize-22)
            {

                ifile >> dupPack.data[k++];
            }
            if(ifile)
                dupPack.H.F = '0';
            else
                dupPack.H.F = '1';
            ifile.close();
        }
        bzero(buffer,packetsize);
        for(i=0;i<10;i++)
            buffer[j++] = dupPack.H.SEQ[i];
    for(i=0;i<10;i++)
        buffer[j++] = '0';
    buffer[j++] = dupPack.H.F;
    buffer[j++] = dupPack.H.A;
    for(i = 0; i<k; i++)
        buffer[j++] = dupPack.data[i];
}

void* timertimeout(void *s)
{
    sleep(tcnt);
    int *pseq = (int *)s;
    char buffer[packetsize];
    socklen_t len = sizeof(caddr);

    do
    {
            if(*pseq == sfseq)
            {
                phase=-1;
                cwnd=1;
                cout << "Timed Out!!! Resending " << *pseq <<endl;
                cout << "Switch to slow start\n";
                createDupPack(buffer,ack);
                sendto(server_fd,buffer,strlen(buffer),0,(struct sockaddr *)&caddr,len);
                break;
            }
            tcnt *=2;
            sleep(tcnt);

    }while(*pseq >= sfseq);

}
int scnt=0;     //slow start count
        int acnt=0;     //congestion avoidance count
void* sendFile(void *f)
{

        pthread_t timer;
        int *s,i,n;
        char buffer[packetsize];
        file_name = (char*) malloc(sizeof(char)*strlen(req.data));
        bzero(file_name,strlen(req.data));
        int *fisrtseq = (int *)f;
        for(i = 0; i < strlen(req.data); i++)
        {
            file_name[i] = req.data[i];
        }
        file_name[i] = '\0';
        socklen_t len = sizeof(caddr);
        bzero(buffer,packetsize);
        sending = true;
        createResponse(buffer,file_name);
        sendto(server_fd,buffer,strlen(buffer),0,(struct sockaddr *)&caddr,len);
        scnt++;
        s = new int();
        *s = seq;
        sfseq = seq;
        sn++;
        if(cwnd > rwnd)
            swnd = rwnd;
        else
            swnd = cwnd;

        while(sending)
        {

            while(sn-sf < swnd && sending)
            {
                if(phase==-1)
                    scnt++;
                else
                    acnt++;
                bzero(buffer,packetsize);
                createResponse(buffer,file_name);
                sendto(server_fd,buffer,strlen(buffer),0,(struct sockaddr *)&caddr,len);
                sn++;
            }
            n= rand()%7;
        //    if(n==4)
          //      sleep(1);
            //s = new int();
            if(*s!=sfseq)
            {
            s = new int();
            *s=sfseq;
            pthread_create(&timer,0,&timertimeout,(void*) s);
            }
           // usleep(200);
        }


}

void* receiveAck(void *f)
{
    socklen_t len = sizeof(caddr);
    int pack,cnt;
    int k =0,i;
    int *firstseq = (int *)f;
    int fs=*firstseq;
    receiving = true;
    cnt=0;
    unsigned int t;
    char bufr[packetsize];

    while(receiving)
    {
        recvfrom(server_fd,data,packetsize,0,(struct sockaddr *)&caddr,&len);
        parseRequest();
        cout << "ack received = " << ack << endl;

        t = sfseq+(swnd*(packetsize+1));        //last ack number expected in window
        if(ack<sfseq )                          //ignoring previous ack because window moved by cummilative ack
            continue;
        if(ack > t)
        {
            k = (ack-sfseq)/(packetsize+1);
            //shiftWindow(k);
            sfseq=ack;
            for(i=0;i<swnd;i++)
                ackarray[i]=0;
            createDupPack(bufr,ack);
            sendto(server_fd,bufr,strlen(bufr),0,(struct sockaddr *)&caddr,len);
            continue;
        }
        else if (ack == sfseq)
        {
            dupAck++;

            if((phase != 0 && dupAck==3 ) || (phase == 0 && dupAck > ssthresh))
            {
                dupAck=0;
                phase =0;           //fast recovery
                cout << "\nSwitch to fast recovery\n";
                ssthresh=swnd/2;
                cwnd = ssthresh+3;
                createDupPack(bufr,ack);
                //tcnt=2;
                sendto(server_fd,bufr,strlen(bufr),0,(struct sockaddr *)&caddr,len);
                continue;
            }

        }
        pack = ((ack-sfseq)/1223)-1;
        if (pack >= 0)
            ackarray[pack]++;

        if (phase == -1)
        {
                if(ackarray[pack] == 1 )        //new ack
                {
                    tcnt = 1;
                     cnt++;
                       int k=0;
                        for(i=0;i<swnd;i++)
                            if(ackarray[i] == 1)
                                k=i;
                        if(k>0)
                            shiftWindow(k+1);
                        else if(ackarray[0] == 1)
                            shiftWindow(1);


                    if(cnt%swnd == 0)
                    {
                        cnt=0;
                        cwnd = cwnd*2;

                        if (cwnd>=ssthresh)                //slow start phase
                        {
                            phase = 1;
                            cout << "\nSwitch to congestion avoidance!!\n";
                        }

                    }
                }

        }

        else if(phase == 1)
        {
           if(ackarray[pack] == 1 )        //new ack
                {
                tcnt=1;
                cnt++;
                       int k=0;
                        for(i=0;i<swnd;i++)
                            if(ackarray[i] == 1)
                                k=i;
                        if(k>0)
                            shiftWindow(k+1);
                        else if(ackarray[0] == 1)
                            shiftWindow(1);


                    if(cnt%swnd == 0)
                    {
                        cnt=0;
                        cwnd = cwnd+1;
                    }
                }

        }
        else
        {
            if(pack < 0)
                cwnd *=2;               //  incrementing cwnd in case of fast recovery
            else if(ackarray[pack] == 1 )        //new ack
                {
                        //cnt++;
                        cwnd = ssthresh;
                        dupAck=0;
                        int k=0;
                        for(i=0;i<swnd;i++)
                            if(ackarray[i] == 1)
                                k=i;
                        if(k>0)
                            shiftWindow(k+1);
                        else if(ackarray[0] == 1)
                            shiftWindow(1);
                        cout << "Switch to Congestion Avoidance!!\n";
                        phase = 1;
                }
        }
                if(cwnd > rwnd)
                    swnd = rwnd;
                else
                    swnd = cwnd;
        }


}

void read_write()
{
    pthread_t sendingThread;
    pthread_t receivingThread;
    int n,i=0,j;
    socklen_t len = sizeof(caddr);
    unsigned int temp;
    while(true)
    {
        fp=0;
        sn=0;sf=0;
        cwnd=1;
        ssthresh=64;
        bzero(ackarray,sizeof(ackarray));
        bzero(data,packetsize);
        n = recvfrom(server_fd,data,packetsize,0,(struct sockaddr *)&caddr,&len);
        if ( n < 0)
            cout << "\nNothing Received\n";
        else
        {
            parseRequest();
            seq = rand() % 1000;
            temp = seq;
            pthread_create(&sendingThread,0,&sendFile,(void*) &temp);
        }

        pthread_create(&receivingThread,0,&receiveAck,(void*) &temp);

        pthread_join( sendingThread, NULL);
        pthread_join( receivingThread, NULL);

        cout << "\n\nPackets sent in slow start = " << scnt << endl;
        cout << "\nPackets sent in congestion avoidance = " << acnt << endl;
        cout << "\n\nfinished";
 	}
}

int main(int argc, char *argv[])
{
    int i;
    if (argc < 2)
    {
	cout << "\nInsufficient Arguments\n";
                return 0;
    }

    for (i =0; i < argc; i++)
    {
        if(argv[i] == NULL)
        {
                cout << "\nInsufficient Arguments\n";
                return 0;
        }
    }
    int t = atoi(argv[2]);
    rwnd = t;

    ackarray =(int*) malloc(sizeof(int)*t);
    createSocket(argv[1]);
    read_write();

    return 0;
}
