/*
P2P_HOST_PORT.H

The UDP port of a host's tunnel (p2p.c), after ChupathingyCE's
p2p_host_port.h. network.tunnel_port, when set (not 0), is the tunnel's
from the start, for joining as well as hosting, and nothing else is tried.
Otherwise every copy of the game starts on a random port, as a joiner needs
no fixed one, and moves to network.host_port (2302, Halo's own, on the
desktop and Android) when it starts hosting, or to the next free one of
P2P_HOST_PORT_TRIES (another copy of the game, or a dedicated server, on the
same machine may hold it); with all of them taken, the random port stays.
network.host_port 0 keeps the random port when hosting too.

(Chupathingy tells a chosen network.tunnel_port from its default by whether
the player's file gives it; our config.toml writes every setting out, so
the host's port is a setting of its own here.)

Header only.
*/

#ifndef P2P_HOST_PORT_H
#define P2P_HOST_PORT_H

/* the host port and the ones after it a host tries */
#define P2P_HOST_PORT_TRIES 5

/* the ports a host's tunnel tries, in order, into ports (room for
P2P_HOST_PORT_TRIES): none while network.tunnel_port is set (the tunnel has
it from the start) or for a host port of 0; else the host port and the ones
after it. Returns the count */
static inline int p2p_host_ports(long tunnel_port, long host_port, unsigned short *ports)
{
	int count = 0;

	if (tunnel_port > 0 || host_port <= 0 || host_port > 65535)
		return 0;
	while (count < P2P_HOST_PORT_TRIES && host_port + count <= 65535)
	{
		ports[count] = (unsigned short)(host_port + count);
		count++;
	}
	return count;
}

/* whether the tunnel's port (bound) is one of the ports a host tries already */
static inline int p2p_host_port_kept(unsigned short bound, const unsigned short *ports, int count)
{
	int index;

	for (index = 0; index < count; index++)
	{
		if (ports[index] == bound)
			return 1;
	}
	return 0;
}

#endif
