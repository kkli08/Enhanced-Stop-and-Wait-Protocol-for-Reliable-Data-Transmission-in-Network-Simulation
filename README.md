# Enhanced-Stop-and-Wait-Protocol-for-Reliable-Data-Transmission-in-Network-Simulation

## Description
This project involves enhancing the traditional stop-and-wait data link protocol for reliable communication in a simulated network environment using the CNET simulator. The primary objective is to ensure reliable transmission between two nodes in a network by handling data and acknowledgment frames effectively. The protocol is designed to work in a setting where piggybacking and negative acknowledgements are not utilized, focusing instead on the core functionality of stop-and-wait mechanisms.

## Technical Highlights
*  **Frame Structure Design**: The protocol defines a `FRAME` structure that includes essential fields such as source and destination addresses, sequence and acknowledgment numbers, checksum, and payload data. This design is crucial for handling various aspects of frame transmission and reception.
*  **Connection State Management**: A `SWCONN` structure is used to maintain the state of the connection, including sequence numbers for the next data frame to send, the expected acknowledgment, and the last message received. This aids in tracking the progress of data exchange and ensuring reliable communication.
*  **Reliable Transmission**: The protocol employs a stop-and-wait mechanism, where the sender waits for an acknowledgment of each data frame before sending the next. This approach is fundamental in ensuring reliable transmission but can lead to lower throughput, a trade-off inherent in the protocol design.
*  **Checksum for Data Integrity**: To ensure the integrity of the data, the protocol computes a checksum for each frame. This mechanism helps in detecting errors during transmission, allowing for retransmission of corrupted frames.
*  **Hop Count Tracking**: An innovative feature of this implementation is the tracking of hop counts in frames, providing insights into the path taken by the frame through the network and potentially enabling route optimization.

## Challenges and Solutions:
- **Efficient Frame Handling**: Managing the transmission and reception of frames in a network with potential errors and delays was challenging. The solution involved implementing robust error detection (using checksums) and retransmission strategies (stop-and-wait).
- **Sequence Number Management**: Ensuring the correct sequence of frames, especially in the face of potential loss or duplication, was another challenge. The protocol addresses this by employing sequence and acknowledgment numbers for tracking the frames.
- **Network Route Optimization**: Identifying the shortest path for data transmission in a dynamic network was an added complexity. The implementation of hop count tracking in the protocol aids in optimizing the route selection over time.

### This project demonstrates a solid understanding of fundamental networking principles, specifically in designing and implementing a reliable data transmission protocol. The enhancements made to the traditional stop-and-wait protocol showcase innovative thinking and problem-solving skills in network programming.

## Please do not copy
This project is uploaded solely for the purpose of portfolio demonstration.