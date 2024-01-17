#include <cnet.h>
#include <stdlib.h>
#include <string.h>

/*  This is an implementation of a stop-and-wait data link protocol.

    This protocol provides a reliable data-link layer for a 2-node network.
    This protocol employs only data and acknowledgement frames -
    piggybacking and negative acknowledgements are not used.

    It is based on Tanenbaum's 'protocol 4', 2nd edition, p227.
 */

static void send_find_path_frame(CnetEvent ev, CnetTimerID timer, CnetData data);

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
    int         Is_find_path_frame;  // 1 if the frame is a find path frame, 0 otherwise
    //  fields for finding the shortest path
    int         hop_count;  // an int value to store the hop count (how many nodes the message has passed through)
    CnetAddr    host_list[14];  // an array to store the list of nodes that the message has passed through
    int         host_list_index;  // an int value to store the index of the Host_list array
    int         host_hop_count[14];  // an array to store the hop count of each node that the message has passed through
    int         shortest_link;  // an int value to store the shortest link
//  THE LAST FIELD IN THE FRAME IS THE PAYLOAD, OUR MESSAGE
    MSG          msg;
} FRAME;


//  a SWCONN struct to hold the connection state
typedef struct {
    CnetAddr    src,dest; 	// source and destination connection addresses
    CnetTimerID lasttimer;
    FRAME       lastframe;
    int         ackexpected, frameexpected, nextframetosend;
    //  fields for finding the shortest path   
    int         found_shortest_path;  // 1 if the shortest path has been found, 0 otherwise
    //  table for storing the shortest path
    CnetAddr    host_list[14];  // an array to store the list of nodes that the message has passed through
    int         host_hop_count[14];  // an array to store the hop count of each node that the message has passed through
    int         hosts_num;  // an int value to store the number of hosts in the network

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
    swconn.found_shortest_path = 0;
    swconn.hosts_num = 0;
    
    for (int i = 0; i < 14; i++){
        swconn.host_list[i] = -1;
        swconn.host_hop_count[i] = -1;
    }
}

void transmit_frame_to_next_hop(FRAME frame, int link){
    // update the frame
    frame.hop_count += 1;
    frame.host_list[frame.host_list_index] = nodeinfo.address;
    frame.host_hop_count[frame.host_list_index] = frame.hop_count;
    frame.host_list_index += 1;

    // send the frame to the next hop
    size_t len = sizeof(FRAME);
    CHECK(CNET_write_physical(link, &frame, &len));
    printf("send frame to the next hop\n");
}
void transmit_msg_frame_to_next_hop(FRAME frame, int link){
    // transmit the frame to the next hop
    if (frame.ack < 0){
        if (frame.seq > -1){
            // DATA transmit
            printf("DATA transmitted:  ");
            FRAME_print (&frame);
        }
    }
    else {
        // ACK transmit
        printf("ACK sent:  ");
        FRAME_print (&frame);
    }
    // tranmit the frame to the next hop
    CHECK(CNET_write_physical(link, &frame, &frame.len));
}

//  A FUNCTION TO TRANSMIT EITHER A DATA OR AN ACKNOWLEDGMENT FRAME
void transmit_frame(CnetAddr destaddr, MSG *msg, size_t length, int seqno, int ackno, int link)
{
    FRAME       frame;

//  INITIALISE THE FRAME'S HEADER FIELDS
    frame.src       = nodeinfo.address;
    frame.dest      = destaddr;
    frame.seq       = seqno;
    frame.ack       = ackno;
    frame.checksum  = 0;
    frame.len       = length;
    frame.shortest_link = link;
    frame.Is_find_path_frame = 0;

    

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

            swconn.lasttimer = CNET_start_timer(EV_TIMER1, 9 * timeout, 0);
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
    printf("sending frame checksum: %d\n", frame.checksum);

    CHECK(CNET_write_physical(link, &frame, &length));
}

//  THE APPLICATION LAYER HAS A NEW MESSAGE TO BE DELIVERED
EVENT_HANDLER(application_ready)
{
    // disable the application layer before find the shortest path
    // CNET_disable_application(ALLNODES);
    // printf("application ready\n");

    CnetAddr destaddr;
    FRAME    frame;
    int     shortest_link;
    lastmsglength  = sizeof(MSG);

    // read the message from the application layer
    CHECK(CNET_read_application(&destaddr, &lastmsg, &lastmsglength));
    printf("\n>>>>>> ready to transmit the message!!!\n");

    CNET_disable_application(ALLNODES);

    // find the shortest path to the destination!
    // and find the link lead to the shortest path!
    for (int i = 0; i < 14; i++ ){
        if (swconn.host_list[i] == destaddr){
            if (swconn.hosts_num - swconn.host_hop_count[i] > swconn.host_hop_count[i]){
                shortest_link = 1;
            }
            else{
                shortest_link = 2;
            }
            break;
        }
        else{
            shortest_link = 1;
        }
    }

    // transmit the frame
    int ackno = -1;
    transmit_frame(destaddr, &lastmsg, lastmsglength, nextdatatosend, ackno, shortest_link);
    // add to swconn
    swconn.dest = destaddr;
    frame.src       = nodeinfo.address;
    frame.dest      = destaddr;
    frame.seq       = nextdatatosend;
    frame.ack       = ackno;
    // frame.checksum  = 0;
    frame.len       = lastmsglength;
    frame.shortest_link = shortest_link;
    frame.Is_find_path_frame = 0;
    memcpy(&frame.msg, &lastmsg, lastmsglength);
    swconn.lastframe = frame;
    // increment # for nextdatatosend
    increment(nextdatatosend);
    // add to swconn
    swconn.nextframetosend = nextdatatosend;
    
}

//  PROCESS THE ARRIVAL OF A NEW FRAME, VERIFY CHECKSUM, ACT ON ITS FRAMEKIND
EVENT_HANDLER(physical_ready)
{
    FRAME        frame;
    int          link;
    int          arriving_checksum, stored_checksum;
    size_t	 len = sizeof(FRAME);

    //  RECEIVE THE NEW FRAME
    CHECK(CNET_read_physical(&link, &frame, &len));

    //  check if the frame is carrying message or just finding shortest path
    if (frame.Is_find_path_frame == 1){
        // handle the frame that is finding shortest path

        // find if the frame is back to the src or not
        // if not, forward the frame to the next hop
        if (frame.src != nodeinfo.address){
            // forward the frame to the next hop and update the frame
            for(int i = 1; i <= nodeinfo.nlinks; i++){
                if (i != link){
                    transmit_frame_to_next_hop(frame, i);
                    break;
                }
            }
        }
        else{
            // if back to the src, store the table into swconn
            swconn.found_shortest_path = 1;
            swconn.hosts_num = frame.hop_count;
            // swconn.host_list = frame.host_list;
            // swconn.host_hop_count = frame.host_hop_count;
            for (int i = 0; i < 14 ; i++){
                swconn.host_list[i] = frame.host_list[i];
                swconn.host_hop_count[i] = frame.host_hop_count[i];
            }
            
            // print the table
            // printf("Shortest path table:\n");
            // for (int i = 0; i < 14 ; i++){
            //     printf("host_list[%d] = %d, host_hop_count[%d] = %d\n", i, swconn.host_list[i], i, swconn.host_hop_count[i]);
            // }
        }
    }
    else{
        // handle the frame that is carrying message
        //  if the frame.dest is not the node, forward the frame to the next hop
        if (frame.dest == nodeinfo.address){
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
                if(frame.seq == ackexpected) {
                    printf("ACK received:  ");
                    FRAME_print (&frame);
                    CNET_stop_timer(swconn.lasttimer);
                    increment(ackexpected);
                    // add to swconn
                    swconn.ackexpected = ackexpected;
                    CNET_enable_application(ALLNODES);
                }
            }
            else {
                // DATA receive
                if(frame.seq == dataexpected) {
                    printf("DATA received:  ");
                    FRAME_print (&frame);
                    len = frame.len;
                    CHECK(CNET_write_application(&frame.msg, &len));
                    increment(dataexpected);
                    // add to swconn
                    swconn.frameexpected = dataexpected;
                    // int ackno = frame.seq;                
                    // transmit_frame(frame.src, NULL, 0, frame.seq, ackno, link);	// acknowledge the data
                }
                // else
                    // printf(">> 2 << BAD frame received:  checksums  (stored=%d, computed=%d)\n", arriving_checksum, stored_checksum);
                int ackno = frame.seq;                
                transmit_frame(frame.src, NULL, 0, frame.seq, ackno, link);	// acknowledge the data
            }
        }
        else{
            // forward the frame to the next hop
            for(int i = 1; i <= nodeinfo.nlinks; i++){
                if (i != link){
                    transmit_msg_frame_to_next_hop(frame, i);
                    break;
                }
            }
        }
    }
}

//  WHEN A TIMEOUT OCCURS, WE RE-TRANSMIT THE MOST RECENT DATA (MESSAGE)
EVENT_HANDLER(timeouts)
{
    FRAME   lastframe;
    
    int ackno = -1;

    lastframe = swconn.lastframe;
    transmit_frame(lastframe.dest, &lastframe.msg, lastframe.len, lastframe.seq, ackno, lastframe.shortest_link);
}

//  DISPLAY THE CURRENT SEQUENCE NUMBERS WHEN A BUTTON IS PRESSED
EVENT_HANDLER(showstate)
{
    printf(
    "\n\tackexpected\t= %i\n\tnextdatatosend\t= %i\n\tdataexpected\t= %i\n",
		    ackexpected, nextdatatosend, dataexpected);
}

//  THIS FUNCTION IS CALLED ONCE, AT THE BEGINNING OF THE WHOLE SIMULATION
EVENT_HANDLER(reboot_node)
{
    //  INDICATE THE EVENTS OF INTEREST FOR THIS PROTOCOL
    if (nodeinfo.nodetype == NT_HOST) {
        CHECK(CNET_set_handler( EV_APPLICATIONREADY, application_ready, 0));
        
        // send find patt message to the linked node
        CNET_set_handler(EV_TIMER2, send_find_path_frame, 0);
            
        // Request EV_TIMER2 in 1 sec, ignore return value
        CNET_start_timer (EV_TIMER2, 1000000, 0);
    }
    CHECK(CNET_set_handler( EV_PHYSICALREADY,    physical_ready, 0));
    CHECK(CNET_set_handler( EV_TIMER1,           timeouts, 0));

//  BIND A FUNCTION AND A LABEL TO ONE OF THE NODE'S BUTTONS
    CHECK(CNET_set_handler( EV_DEBUG0,           showstate, 0));
    CHECK(CNET_set_debug_string( EV_DEBUG0, "State"));
    
    if(nodeinfo.nodenumber == 0 )
	CNET_enable_application(ALLNODES);

    // init SWCONN
    SWCONN_init();
}


static EVENT_HANDLER(send_find_path_frame){
    FRAME frame;
    frame.src = nodeinfo.address;
    frame.hop_count = 0;
    frame.host_list_index = 0;
    frame.Is_find_path_frame = 1;
    // frame.checksum = 0;

    // if the shortes path table is not empty, then send the table to the linked node
    if (swconn.found_shortest_path == 0){
        // send this frame to the linked node with link number 1
        size_t len = sizeof(FRAME);
        // frame.checksum = CNET_ccitt((unsigned char *)&frame, len);
        CHECK(CNET_write_physical(1, &frame, &len));
        printf("send find path frame to the host with link == 1\n");
        // Request EV_TIMER2 in 8 sec, ignore return value
        // CNET_start_timer (EV_TIMER2, 15000000, 0);
    }
}

