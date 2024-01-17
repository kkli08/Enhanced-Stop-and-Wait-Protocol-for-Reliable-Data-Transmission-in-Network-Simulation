#include <cnet.h>
#include <stdlib.h>
#include <string.h>

/*  This is an implementation of a stop-and-wait data link protocol.

    This protocol provides a reliable data-link layer for a 2-node network.
    This protocol employs only data and acknowledgement frames -
    piggybacking and negative acknowledgements are not used.

    It is based on Tanenbaum's 'protocol 4', 2nd edition, p227.
 */

//  A FRAME CAN BE EITHER DATA OR AN ACKNOWLEDGMENT FRAME

//  DATA FRAMES CARRY A MAXIMUM-SIZED PAYLOAD, OUR MESSAGE
typedef struct {
    char        data[MAX_MESSAGE_SIZE];
} MSG;


//  THE FORMAT OF A FRAME
typedef struct {
    //  THE FIRST FIELDS IN THE STRUCTURE DEFINE THE FRAME HEADER
    CnetAddr    src,dest; 	// source and destination node addresses
    size_t	    len;       	// the length of the msg field only
    int         checksum;  	// checksum of the whole frame
    int         seq;        // seq > 0 for valid data, else = -1
    int         ack;        // ack > 0 for valid ack, else = -1    

    // fields for the shortest path
    int         hop_count;  // an int value to store the hop count (how many nodes the message has passed through)
    //  THE LAST FIELD IN THE FRAME IS THE PAYLOAD, OUR MESSAGE
    MSG          msg;
} FRAME;

//  a SWCONN struct to hold the connection state
typedef struct {
    CnetAddr    src,dest; 	// source and destination connection addresses
    CnetTimerID lasttimer;
    FRAME       lastframe;
    int         ackexpected, frameexpected, nextframetosend;
    // table for finding the shortest path
    int         found_shortest_path[14];  // 1 if the shortest path has been found, 0 otherwise
    CnetAddr    host_list[14];  // an array to store the list of nodes that the message has passed through
    int         host_hop_count[14];  // an array to store the hop count of each node that the message has passed through
    MSG         last_receive_msg;  // the last message received
} SWCONN;



//  SOME HELPFUL MACROS FOR COMMON CALCULATIONS
#define FRAME_HEADER_SIZE	(sizeof(FRAME) - sizeof(MSG))
#define FRAME_SIZE(frame)	(FRAME_HEADER_SIZE + frame.len)
#define increment(seq)		seq = 1-seq


//  STATE VARIABLES HOLDING INFORMATION ABOUT THE LAST MESSAGE
SWCONN      swconn; // only one connection in this part

MSG       	lastmsg;
size_t		lastmsglength		= 0;
CnetTimerID	lasttimer		= NULLTIMER;

//  STATE VARIABLES HOLDING SEQUENCE NUMBERS
int		nextdatatosend		= 0;
int       	ackexpected		= 0;
int		dataexpected		= 0;


//  A Function to print a frame
void FRAME_print (FRAME *f) {
    printf ("(src= %d, dest= %d, seq= %d, ack= %d, msgLen= %ld) \n",
     	    f->src, f->dest, f->seq, f->ack, f->len);
}

//  A function to init the connection state
void SWCONN_init(){
    swconn.src = nodeinfo.address;
    swconn.dest = -1;
    swconn.lasttimer = NULLTIMER;
    swconn.ackexpected = 0;
    swconn.frameexpected = 0;
    swconn.nextframetosend = 0;

    strcpy(swconn.last_receive_msg.data, "Initialise");

    for (int i = 0; i < 14; i++){
        swconn.host_list[i] = -1;
        swconn.host_hop_count[i] = -1;
        swconn.found_shortest_path[i] = -1;
    }
}
//  A FUNCTION TO TRANSMIT EITHER A DATA OR AN ACKNOWLEDGMENT FRAME
void transmit_frame(CnetAddr srcaddr, CnetAddr destaddr, MSG *msg, size_t length, int seqno, int ackno, int link, int hop_count)
{
    FRAME       frame;


    //  INITIALISE THE FRAME'S HEADER FIELDS
    frame.src       = srcaddr;
    frame.dest      = destaddr;
    frame.seq       = seqno;
    frame.ack       = ackno;
    frame.checksum  = 0;
    frame.len       = length;

    
    if (ackno < 0){
        if (seqno > -1){
            frame.hop_count = hop_count;
            // DATA transmit
            CnetTime	timeout;

            printf("DATA transmitted:  ");
            FRAME_print (&frame);
            memcpy(&frame.msg, msg, length);

            if (srcaddr == nodeinfo.address){
                frame.hop_count = 0;
                timeout =
                    FRAME_SIZE(frame)*((CnetTime)8000000 / linkinfo[link].bandwidth) +
                            linkinfo[link].propagationdelay;

                swconn.lasttimer = CNET_start_timer(EV_TIMER1, 9 * timeout, 0);
            }
        }
    }
    else {
        // ACK transmit
        frame.hop_count = hop_count;
        if (srcaddr == nodeinfo.address){
            printf("ACK sent:  ");
        }
        else{
            printf("ACK transmitted:  ");
        }
        FRAME_print (&frame);
    }
    

    //  FINALLY, WRITE THE FRAME TO THE PHYSICAL LAYER
    length		= FRAME_SIZE(frame);
    frame.checksum	= CNET_ccitt((unsigned char *)&frame, length);
    CHECK(CNET_write_physical(link, &frame, &length));
}

//  THE APPLICATION LAYER HAS A NEW MESSAGE TO BE DELIVERED
EVENT_HANDLER(application_ready)
{
    CnetAddr destaddr;
    FRAME   lastframe;
    int    link;

    lastmsglength  = sizeof(MSG);
    CHECK(CNET_read_application(&destaddr, &lastmsg, &lastmsglength));
    CNET_disable_application(ALLNODES);

    int ackno = -1;
    int have_shortest_path = 0;
    // check if the destaddr has the shortest path
    for (int i = 0; i < 14; i++){
        if (swconn.host_list[i] == destaddr){
            if (swconn.found_shortest_path[i] > 0){
                have_shortest_path = 1;
                link = swconn.found_shortest_path[i];
                transmit_frame(nodeinfo.address, destaddr, &lastmsg, lastmsglength, nextdatatosend, ackno, link, 0);
                break;
            }
        }
    }
    if (have_shortest_path == 0){
        for (int i = 1; i <= nodeinfo.nlinks; i++){
            transmit_frame(nodeinfo.address, destaddr, &lastmsg, lastmsglength, nextdatatosend, ackno, i, 0);
        }
    }
    // add to swconn
    swconn.dest = destaddr;

    lastframe.src       = nodeinfo.address;
    lastframe.dest      = destaddr;
    lastframe.seq       = nextdatatosend;
    lastframe.ack       = ackno;
    lastframe.checksum  = 0;
    lastframe.len       = lastmsglength;
    memcpy(&lastframe.msg, &lastmsg, lastmsglength);
    swconn.lastframe = lastframe;

    // increment # for nextdatatosend
    increment(nextdatatosend);
    // add to swconn
    swconn.nextframetosend = nextdatatosend;
}

//  PROCESS THE ARRIVAL OF A NEW FRAME, VERIFY CHECKSUM, ACT ON ITS FRAMEKIND
EVENT_HANDLER(physical_ready)
{
    FRAME        frame;
    int          link, arriving_checksum, stored_checksum;
    size_t	 len = sizeof(FRAME);

    //  RECEIVE THE NEW FRAME
    CHECK(CNET_read_physical(&link, &frame, &len));

    if (frame.dest != nodeinfo.address){
        frame.hop_count += 1;
        // forward the frame to the next hop and update the frame
        for(int i = 1; i <= nodeinfo.nlinks; i++){
            if (i != link){
                transmit_frame(frame.src, frame.dest, &frame.msg, frame.len, frame.seq, frame.ack, i, frame.hop_count);
                break;
            }
        }
    }
    else{
        //  CALCULATE THE CHECKSUM OF THE ARRIVING FRAME, IGNORE IF INVALID
        arriving_checksum	= frame.checksum;
        frame.checksum  	= 0;
        stored_checksum = CNET_ccitt((unsigned char *)&frame, len);
        if(stored_checksum != arriving_checksum) {
            printf("BAD frame received:  checksums  (stored=%d, computed=%d)\n", arriving_checksum, stored_checksum);
            return;           // bad checksum, just ignore frame
        }


        //  use if statement to determine if frame is data or ack
        if (frame.ack > -1){
            // ACK receive
            // if(frame.seq == swconn.ackexpected) {
            printf("ACK received:  ");
            FRAME_print (&frame);
            CNET_stop_timer(swconn.lasttimer);
            increment(swconn.ackexpected);
            // update swconn shortest path table
            for (int i = 0; i < 14; i++){
                if (swconn.host_list[i] == -1){
                    swconn.host_list[i] = frame.src;
                    swconn.found_shortest_path[i] = link;
                    swconn.host_hop_count[i] = frame.hop_count;
                    break;
                }
                else if (swconn.host_list[i] == frame.src){
                    if (swconn.host_hop_count[i] > frame.hop_count){
                        swconn.found_shortest_path[i] = link;
                        swconn.host_hop_count[i] = frame.hop_count;
                    }
                    break;
                }
            }
            
            CNET_enable_application(ALLNODES);
            // }
        }
        else {
            // DATA receive
            printf("DATA received:  ");
            FRAME_print (&frame);
            len = frame.len;
            if (strcmp(swconn.last_receive_msg.data, frame.msg.data) != 0){
                CHECK(CNET_write_application(&frame.msg, &len));
                memcpy(swconn.last_receive_msg.data,frame.msg.data, MAX_MESSAGE_SIZE);
                increment(swconn.frameexpected);
            }
            
            int ackno = frame.seq;
            frame.hop_count += 1;
            transmit_frame(nodeinfo.address, frame.src, NULL, 0, frame.seq, ackno, link, frame.hop_count);	// acknowledge the data        
        }
    }
}

//  WHEN A TIMEOUT OCCURS, WE RE-TRANSMIT THE MOST RECENT DATA (MESSAGE)
EVENT_HANDLER(timeouts)
{
    FRAME   lastframe;
    
    int ackno = -1;

    lastframe = swconn.lastframe;
    transmit_frame(nodeinfo.address, lastframe.dest, &lastframe.msg, lastframe.len, lastframe.seq, ackno, 1, 0);
}

//  DISPLAY THE CURRENT SEQUENCE NUMBERS WHEN A BUTTON IS PRESSED
EVENT_HANDLER(showstate)
{
    printf("------------------------\n");
    printf("Shortest path table:  \n");
    for (int i = 0; i < 14; i++){
        if (swconn.host_list[i] != -1){
            printf("HOST[%d] TRANSLINK[%d] HOP_COUNT[%d]\n", swconn.host_list[i], swconn.found_shortest_path[i], swconn.host_hop_count[i]);
        }
    }
    printf("------------------------\n");
}

//  THIS FUNCTION IS CALLED ONCE, AT THE BEGINNING OF THE WHOLE SIMULATION
EVENT_HANDLER(reboot_node)
{


//  INDICATE THE EVENTS OF INTEREST FOR THIS PROTOCOL
    if (nodeinfo.nodetype == NT_HOST) {
        CHECK(CNET_set_handler( EV_APPLICATIONREADY, application_ready, 0));
    }
    CHECK(CNET_set_handler( EV_PHYSICALREADY,    physical_ready, 0));
    CHECK(CNET_set_handler( EV_TIMER1,           timeouts, 0));

//  BIND A FUNCTION AND A LABEL TO ONE OF THE NODE'S BUTTONS
    CHECK(CNET_set_handler( EV_DEBUG0,           showstate, 0));
    CHECK(CNET_set_debug_string( EV_DEBUG0, "State"));

    if(nodeinfo.nodenumber == 0)
	CNET_enable_application(ALLNODES);

    // init SWCONN
    SWCONN_init();
}
