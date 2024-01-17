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
    // new fields for part 3
    int         hop_count;  // an int value to store the hop count (how many nodes the message has passed through)
    int         link_used_in_src;  // an int value to store the link number of the link used in the src
    int         shortest_path_link;  // an int value to store the link number of the shortest path in the src, -1 means haven't found the shortest path yet
    int         found_shortest_path;  // 1 if the shortest path has been found, 0 otherwise
//  THE LAST FIELD IN THE FRAME IS THE PAYLOAD, OUR MESSAGE
    MSG          msg;
} FRAME;

//  a SWCONN struct to hold the connection state
typedef struct {
    CnetAddr    src,dest; 	// source and destination connection addresses
    CnetTimerID lasttimer;
    FRAME       lastframe;
    int         ackexpected, frameexpected, nextframetosend;
    int         link;  // the link number of the link used in the src
} SWCONN;

//  A struct that stores the shortest path to a node after receiving an ack message from that node
typedef struct {
    CnetAddr    dest;       // the destination node address
    int         found;  // 1 if the shortest path has been found, 0 otherwise
    int       shortest_path_link;  // the link number of the shortest path in src to dest
}SHORTEST_PATH_TABLE_SENDER;

//  A struct that stores the path both in clock-wise and anti-clock-wise directions after receiving the data message
typedef struct {
    int         received;  // number of times the data message has been received
    CnetAddr    src;        // the source node address
    int        clock_wise_link;   // the link number of the clock-wise path in src
    int         clock_wise_path_length; // the length of the clock-wise path
    int        anti_clock_wise_link;   // the link number of the anti-clock-wise path in src
    int         anti_clock_wise_path_length; // the length of the anti-clock-wise path
}SHORTEST_PATH_TABLE_RECEIVER;



//  SOME HELPFUL MACROS FOR COMMON CALCULATIONS
#define FRAME_HEADER_SIZE	(sizeof(FRAME) - sizeof(MSG))
#define FRAME_SIZE(frame)	(FRAME_HEADER_SIZE + frame.len)
#define increment(seq)		seq = 1-seq
#define MAX_PATH_LENGTH 14

//  GLOBAL VARIABLES
SWCONN      swconn[10]; // 10 connections in this part
SHORTEST_PATH_TABLE_SENDER shortest_path_table_sender[14]; // maximum 14 nodes in this part
SHORTEST_PATH_TABLE_RECEIVER shortest_path_table_receiver[14]; // maximum 14 nodes in this part
int shortest_path_table_sender_index = 0;
int shortest_path_table_receiver_index = 0;

//  STATE VARIABLES HOLDING INFORMATION ABOUT THE LAST MESSAGE
MSG       	lastmsg;
size_t		lastmsglength		= 0;
CnetTimerID	lasttimer		= NULLTIMER;

//  STATE VARIABLES HOLDING SEQUENCE NUMBERS
int		nextdatatosend		= 0;
int       	ackexpected		= 0;
int		dataexpected		= 0;

//  if receiced all the addr msg from the neighbour, set to 1
int     identify_shortest_path = 0;
CnetAddr   neighbour_addr[2];

//  A Function to print a frame
void FRAME_print (FRAME *f) {
    printf ("(src= %d, dest= %d, seq= %d, ack= %d, msgLen= %ld) \n",
     	    f->src, f->dest, f->seq, f->ack, f->len);
}

//  A function to init the connection state
void SWCONN_init(int swconn_index){
    swconn[swconn_index].src = nodeinfo.address;
    swconn[swconn_index].dest = -1;
    swconn[swconn_index].lasttimer = NULLTIMER;
    swconn[swconn_index].ackexpected = 0;
    swconn[swconn_index].frameexpected = 0;
    swconn[swconn_index].nextframetosend = 0;
    swconn[swconn_index].link = swconn_index;
}
//  A FUNCTION TO TRANSMIT EITHER A DATA OR AN ACKNOWLEDGMENT FRAME
void transmit_frame(CnetAddr destaddr, MSG *msg, size_t length, int seqno, int ackno, int link, int shortest_path_link, int found_shortest_path)
{
    FRAME       frame;

//  INITIALISE THE FRAME'S HEADER FIELDS
    frame.src       = nodeinfo.address;
    frame.dest      = destaddr;
    frame.seq       = seqno;
    frame.ack       = ackno;
    frame.checksum  = 0;
    frame.len       = length;
    // new fields for part 3
    frame.link_used_in_src = link;
    frame.shortest_path_link = shortest_path_link;
    frame.found_shortest_path = found_shortest_path;
    frame.hop_count = 0;

    if (ackno < 0){
        if (seqno > -1){
            // DATA transmit
            CnetTime	timeout;

            printf("DATA transmitted:  ");
            FRAME_print (&frame);
            memcpy(&frame.msg, msg, length);

            timeout =
                FRAME_SIZE(frame)*((CnetTime)8000000 / linkinfo[link].bandwidth) +
                        linkinfo[link].propagationdelay;

            printf("timeout = %ld\n", timeout);
            for (int i = 1; i <= nodeinfo.nlinks; i++){
                if (swconn[i].link == link){
                    swconn[i].lastframe = frame;
                    swconn[i].lasttimer = CNET_start_timer(EV_TIMER1, 3 * timeout, 0);
                    printf("when transmit data, start timer --> link: %d\n", i);
                    // (CnetData)i
                }
            }
        }
    }
    else {
        // ACK transmit
        printf("ACK sent:  ");
        FRAME_print (&frame);
    }

//  FINALLY, WRITE THE FRAME TO THE PHYSICAL LAYER
    length		= FRAME_SIZE(frame);
    frame.checksum	= CNET_ccitt((unsigned char *)&frame, length);
    printf("src;    checksum: %d\n", frame.checksum);
    CHECK(CNET_write_physical(link, &frame, &length));
}

void transmit_frame_to_next_hop(FRAME frame, int link){
    // int stored_checksum;
    // size_t	 len = sizeof(FRAME);
    if (frame.ack < 0){
        if (frame.seq > -1){
            // DATA transmit
            printf("DATA transmitted:  ");
            FRAME_print (&frame);
            frame.hop_count++;
            // stored_checksum = CNET_ccitt((unsigned char *)&frame, len);
            // printf("non-consecutive; arriving_checksum: %d, stored_checksum: %d\n", frame.checksum, stored_checksum);

        }
    }
    else {
        // ACK transmit
        printf("ACK sent:  ");
        FRAME_print (&frame);
    }

    //  FINALLY, WRITE THE FRAME TO THE PHYSICAL LAYER
    // frame.len		= FRAME_SIZE(frame);
    // frame.checksum	= CNET_ccitt((unsigned char *)&frame, frame.len);
    CHECK(CNET_write_physical(link, &frame, &frame.len));
}


//  THE APPLICATION LAYER HAS A NEW MESSAGE TO BE DELIVERED
EVENT_HANDLER(application_ready)
{
    CnetAddr destaddr;
    FRAME   lastframe;
    lastmsglength  = sizeof(MSG);
    CHECK(CNET_read_application(&destaddr, &lastmsg, &lastmsglength));
    CNET_disable_application(ALLNODES);

    int ackno = -1; 
    lastframe.src       = nodeinfo.address;
    lastframe.dest      = destaddr;
    lastframe.seq       = nextdatatosend;
    lastframe.ack       = ackno;
    lastframe.checksum  = 0;
    lastframe.len       = lastmsglength;
    // increment # for nextdatatosend
    increment(nextdatatosend);

    int have_shortest_path = 0;
    for (int i = 0; i < 14; i++){
        if (shortest_path_table_sender[i].dest == lastframe.dest){
            if (shortest_path_table_sender[i].found == 1){
                have_shortest_path = 1;
                // transmit the shortest path back to the source
                transmit_frame(destaddr, &lastmsg, lastmsglength, nextdatatosend, ackno, shortest_path_table_sender[i].shortest_path_link, shortest_path_table_sender[i].shortest_path_link, 1);
                // update swconn
                int j = shortest_path_table_sender[i].shortest_path_link;
                memcpy(&lastframe.msg, &lastmsg, lastmsglength);
                swconn[j].lastframe = lastframe;
                // add to swconn
                swconn[j].nextframetosend = nextdatatosend;
                break;
            }
            
        }
    }
    if (have_shortest_path == 0){
        // initialize the shortest_path_table_sender for the first time for the destination host
        int init_sender_table = 0;
        for (int i = 0; i < 14; i++){
            if (shortest_path_table_sender[i].dest == destaddr){
                init_sender_table = 1;
                break;
            }
        }
        if (init_sender_table == 0){
            shortest_path_table_sender[shortest_path_table_sender_index].dest = destaddr;
            shortest_path_table_sender[shortest_path_table_sender_index].found = 0;
            shortest_path_table_sender_index++;
        }
        // send msg in both directions to find the shortest path
        for (int i = 1; i <= nodeinfo.nlinks; i++){
            transmit_frame(destaddr, &lastmsg, lastmsglength, nextdatatosend, ackno, i, -1, 0);
            // update swconn
            for (int j = 1; j <= nodeinfo.nlinks; j++){
                memcpy(&lastframe.msg, &lastmsg, lastmsglength);
                swconn[j].lastframe = lastframe;
                // add to swconn
                swconn[j].nextframetosend = nextdatatosend;
            
            }
        }
        
    }
}

//  PROCESS THE ARRIVAL OF A NEW FRAME, VERIFY CHECKSUM, ACT ON ITS FRAMEKIND
EVENT_HANDLER(physical_ready)
{
    FRAME        frame;
    int          link, arriving_checksum, stored_checksum;
    size_t	 len = sizeof(FRAME);

    //  RECEIVE THE NEW FRAME
    CHECK(CNET_read_physical(&link, &frame, &len));

    //  handle the frame
    if (frame.dest == nodeinfo.address && nodeinfo.nodetype == NT_HOST){
        //  CALCULATE THE CHECKSUM OF THE ARRIVING FRAME, IGNORE IF INVALID
        arriving_checksum	= frame.checksum;
        frame.checksum  	= 0;
        stored_checksum = CNET_ccitt((unsigned char *)&frame, len);
        printf("->arrive dest; arriving_checksum: %d, stored_checksum: %d\n", arriving_checksum, stored_checksum);
        if(stored_checksum != arriving_checksum) {
            printf(">>1 BAD frame received:  checksums  (stored=%d, computed=%d)\n",stored_checksum, arriving_checksum);
            return;           // bad checksum, just ignore frame
        }
        //  use if statement to determine if frame is data or ack
        if (frame.ack > -1){
            // ACK receive
            // if(frame.seq == swconn[link].ackexpected) {
                printf("ACK received:  ");
                FRAME_print (&frame);
                if (frame.found_shortest_path == 1){
                    printf("shortest path found: %d\n", frame.shortest_path_link);
                }
                else{
                    printf("shortest path not found\n");
                }
                
                printf("when stop timer, --> link: %d\n", frame.link_used_in_src);
                for (int i = 1; i <= nodeinfo.nlinks; i++){
                    CNET_stop_timer(swconn[i].lasttimer);
                    increment(swconn[i].ackexpected);
                }
                // add to swconn
                // swconn[link].ackexpected = ackexpected;

                // update the SHORTEST_PATH_TABLE_SENDER
                for (int i = 0; i < 14; i++){
                    if (shortest_path_table_sender[i].dest == frame.src && shortest_path_table_sender[i].found == 0 && frame.found_shortest_path == 1){
                        shortest_path_table_sender[i].found = 1;
                        shortest_path_table_sender[i].shortest_path_link = frame.shortest_path_link;
                        break;
                    }
                }
                CNET_enable_application(ALLNODES);
            // }
        }
        else {
            // DATA receive
            // if(frame.seq == swconn[link].frameexpected) {
                printf("DATA received:  ");
                FRAME_print (&frame);
                len = frame.len;
                CHECK(CNET_write_application(&frame.msg, &len));
                increment(swconn[link].frameexpected);
                // add to swconn
            

                // init the SHORTEST_PATH_TABLE_RECEIVER for the first time or update it if it already exists
                int init_receiver_table = 0;
                for (int i = 0; i < 14; i++){
                    if (shortest_path_table_receiver[i].src == frame.src && shortest_path_table_receiver[i].received == 1){
                        init_receiver_table = 1;
                        shortest_path_table_receiver[i].anti_clock_wise_link = frame.link_used_in_src;
                        shortest_path_table_receiver[i].anti_clock_wise_path_length = frame.hop_count;

                        // identify the shortest path and send the message to the source host
                        if (shortest_path_table_receiver[i].clock_wise_path_length < shortest_path_table_receiver[i].anti_clock_wise_path_length){
                            // send the message to the source host
                            frame.shortest_path_link = shortest_path_table_receiver[i].clock_wise_link;
                            
                        }
                        else{
                            // send the message to the source host
                            frame.shortest_path_link = shortest_path_table_receiver[i].anti_clock_wise_link;
                            }
                        frame.found_shortest_path = 1;
                        break;
                    }
                }
                // initialize the shortest_path_table_receiver for the first time for the dest host
                if (init_receiver_table == 0){
                    shortest_path_table_receiver[shortest_path_table_receiver_index].src = frame.src;
                    shortest_path_table_receiver[shortest_path_table_receiver_index].received = 1;
                    shortest_path_table_receiver[shortest_path_table_receiver_index].clock_wise_link = frame.link_used_in_src;
                    shortest_path_table_receiver[shortest_path_table_receiver_index].clock_wise_path_length = frame.hop_count;
                    shortest_path_table_receiver_index++;
                }
                
            // }
            // else{
            //     printf(">>2 BAD frame received:  checksums  (stored=%d, computed=%d)\n",stored_checksum, arriving_checksum);
                
            // }
            int ackno = frame.seq;
            transmit_frame(frame.src, NULL, 0, frame.seq, ackno, link, frame.shortest_path_link, frame.found_shortest_path);	// acknowledge the data
            
        }
    }
    else{
        stored_checksum = CNET_ccitt((unsigned char *)&frame, len);
        printf("non-consecutive; arriving_checksum: %d, stored_checksum: %d\n", frame.checksum, stored_checksum);
        if (stored_checksum != frame.checksum) {
            printf("BAD frame received:  checksums  (stored=%d, computed=%d)\n",stored_checksum, frame.checksum);
            return;           // bad checksum, just ignore frame
        }
        //  IF THE FRAME IS NOT ADDRESSED TO ME, send it to the next hop
        for(int i = 1; i <= nodeinfo.nlinks; i++){
            if (i != link){
                transmit_frame_to_next_hop(frame, i);
                break;
            }
        }
    }
        
    

}

//  WHEN A TIMEOUT OCCURS, WE RE-TRANSMIT THE MOST RECENT DATA (MESSAGE)
EVENT_HANDLER(timeouts)
{
    FRAME   lastframe;
    // printf("timeout, seq=%i\n", ackexpected);
    // void transmit_frame(CnetAddr destaddr, MSG *msg, size_t length, int seqno, int ackno)
    int ackno = -1;

    // Retrieve the value of i for this timer event
    // int i;
    // CNET_timer_data(1, (CnetData *)&i);

    // lastframe = swconn[i].lastframe;
    // transmit_frame(lastframe.dest, &lastframe.msg, lastframe.len, lastframe.seq, ackno, i, lastframe.shortest_path_link, lastframe.found_shortest_path);

    for (int i = 1; i <= nodeinfo.nlinks; i++){
        lastframe = swconn[i].lastframe;
        transmit_frame(lastframe.dest, &lastframe.msg, lastframe.len, lastframe.seq, ackno, i, lastframe.shortest_path_link, lastframe.found_shortest_path);
    }

}

//  DISPLAY THE CURRENT SEQUENCE NUMBERS WHEN A BUTTON IS PRESSED
EVENT_HANDLER(showstate)
{
    printf(
    "\n\tackexpected\t= %i\n\tnextdatatosend\t= %i\n\tdataexpected\t= %i\n",ackexpected, nextdatatosend, dataexpected);

}

//  THIS FUNCTION IS CALLED ONCE, AT THE BEGINNING OF THE WHOLE SIMULATION
EVENT_HANDLER(reboot_node)
{
//  ENSURE THAT WE HAVE A 2-NODE NETWORK
    // if(nodeinfo.nodenumber > 1) {
	// fprintf(stderr,"This is not a 2-node network!\n");
	// exit(1);
    // }

//  INDICATE THE EVENTS OF INTEREST FOR THIS PROTOCOL
    if (nodeinfo.nodetype == NT_HOST) {
        CHECK(CNET_set_handler( EV_APPLICATIONREADY, application_ready, 0));
    }

    CHECK(CNET_set_handler( EV_PHYSICALREADY,    physical_ready, 0));
    CHECK(CNET_set_handler( EV_TIMER1,           timeouts, 0));

    for (int i = 1; i <= nodeinfo.nlinks; i++){
        // init SWCONN
        SWCONN_init(i);
    }

//  BIND A FUNCTION AND A LABEL TO ONE OF THE NODE'S BUTTONS
    CHECK(CNET_set_handler( EV_DEBUG0,           showstate, 0));
    CHECK(CNET_set_debug_string( EV_DEBUG0, "State"));

/*  THIS PROTOCOL IS CURRENTLY WRITTEN SO THAT ONLY NODE-0 WILL GENERATE
    AND TRANSMIT MESSAGES, AND NODE-1 WILL RECEIVE THEM.
    THIS RESTRICTION SIMPLIFIES UNDERSTANDING FOR THOSE NET TO cnet.
    THE RESTRICTION CAN EASILY BE REMOVED BY REMOVING THE LINE:

	    if(nodeinfo.nodenumber == 0)

    BOTH NODES WILL THEN TRANSMIT AND RECEIVE (WHY?)
 */
    if(nodeinfo.nodenumber == 0)
	CNET_enable_application(ALLNODES);
}

