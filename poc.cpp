#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/r_c_shortest_paths.hpp>
#include <iostream>
#include <vector>

using namespace boost;
using namespace std;

// BglEdge is what BGL works with
struct BglEdge
{
    double cost;
    int travelTime;
    long long pgId;
    int bglIdx;
};

struct VertexData
{
    int eat, lat;
    long long pgId;
};

typedef adjacency_list<vecS, vecS, directedS, VertexData, BglEdge> Graph;

typedef graph_traits<Graph>::vertex_descriptor Vertex;
typedef graph_traits<Graph>::edge_descriptor Edge;

// Represents the data coming from postgres
struct PgEdge
{
    long long id, sourceId, targetId;
    double cost;
    int travelTime;
};

struct RCGraph
{
    Graph g;
    map<long long, Vertex> pgToBgl;
    map<Vertex, long long> bglToPg;
    map<Edge, long long> edgeToPg;

    RCGraph(vector<VertexData> &vertices, vector<PgEdge> &edges)
    {
        for (VertexData data : vertices)
        {
            Vertex v = add_vertex(data, g);
            pgToBgl[data.pgId] = v;
            bglToPg[v] = data.pgId;
        }

        int idx = 0;
        for (PgEdge data : edges)
        {
            if (pgToBgl.find(data.sourceId) == pgToBgl.end() or
                pgToBgl.find(data.targetId) == pgToBgl.end())
                continue;

            Vertex source = pgToBgl[data.sourceId];
            Vertex target = pgToBgl[data.targetId];

            BglEdge bglEdge = {data.cost, data.travelTime, data.id, idx};
            auto [edge, ok] = add_edge(source, target, bglEdge, g);

            if (ok)
            {
                idx++;
                edgeToPg[edge] = data.id;
            }
        }
    }
};

// To track each state per label (total cost and total time)
struct ResourceState
{
    double totalCost;
    int totalTime;
    bool operator==(const ResourceState &other) const
    {
        return totalCost == other.totalCost and totalTime == other.totalTime;
    }

    bool operator<(const ResourceState &other) const
    {
        if (totalCost == other.totalCost)
            return totalTime < other.totalTime;
        return totalCost < other.totalCost;
    }
};

/*
    REF stands for Resource Extension Function
    Boost named it that based on the research papers the algorithm is based on
    BGL calls this function as REF(graph, newState, oldState, edge)
    Why we need it -> we're writing a function to describe what happens to the
    the states we travel from one node to another
*/
struct REF
{
    bool operator()(const Graph &g, ResourceState &newState,
                    const ResourceState &oldState, Edge edge) const
    {
        newState.totalCost = oldState.totalCost + g[edge].cost;
        newState.totalTime = oldState.totalTime + g[edge].travelTime;
        Vertex tar = target(edge, g);
        if (newState.totalTime < g[tar].eat)
            newState.totalTime = g[tar].eat;

        return newState.totalTime <= g[tar].lat;
    }
};

/*
    Returns true if Path A dominates Path B, false otherwise
    A dominates B if it's better or equal in every resource, and strictly better in at least one
*/
struct DominancePruning
{
    bool operator()(const ResourceState &a, const ResourceState &b) const
    {
        return a.totalCost <= b.totalCost and a.totalTime <= b.totalTime;
    }
};

/*
    BGL internally uses arrays indexed by edge, and it gets
    the index by calling get(edgeIndexMap, edge)
*/
struct EdgeIndexMap
{
    typedef int value_type;
    typedef int reference;
    typedef Edge key_type;
    typedef readable_property_map_tag category;

    const Graph *g;

    EdgeIndexMap(const Graph &graph)
    {
        g = &graph;
    }

    int operator[](const Edge &edge) const
    {
        return (*g)[edge].bglIdx;
    }
};

int get(const EdgeIndexMap &mp, Edge edge) { return mp[edge]; }

struct OutputRow
{
    int aggTime;
    double cost, aggCost;
    long long seq, pathId, node, edge;
};

vector<vector<OutputRow>> calcShortestDistance(RCGraph &rcg, long long &s, long long &t)
{
    vector<vector<Edge>> paretoPaths;
    vector<ResourceState> paretoStates;
    vector<vector<OutputRow>> output;

    Vertex src = rcg.pgToBgl.at(s);
    Vertex tar = rcg.pgToBgl.at(t);

    // gets reversed paths
    r_c_shortest_paths(rcg.g, get(boost::vertex_index, rcg.g),
                       EdgeIndexMap(rcg.g), src, tar,
                       paretoPaths, paretoStates,
                       ResourceState{0, 0}, REF(), DominancePruning());

    long long seqCounter = 1, pathCounter = 1;
    for (auto &path : paretoPaths)
    {
        OutputRow row;
        vector<OutputRow> rows;
        double prefixCost = 0;
        int prefixTime = 0;
        for (int i = path.size() - 1; i >= 0; i--)
        {
            BglEdge edge = rcg.g[path[i]];

            row.cost = edge.cost;

            row.aggCost = prefixCost;
            prefixCost = row.cost + prefixCost;

            row.aggTime = prefixTime;
            prefixTime = edge.travelTime + prefixTime;

            Vertex destination = target(path[i], rcg.g);
            if (prefixTime < rcg.g[destination].eat)
                prefixTime = rcg.g[destination].eat;

            row.seq = seqCounter;
            seqCounter++;

            row.node = rcg.bglToPg.at(source(path[i], rcg.g));

            row.edge = rcg.edgeToPg.at(path[i]);

            row.pathId = pathCounter;

            rows.push_back(row);
        }

        OutputRow terminal;
        terminal.seq = seqCounter, terminal.pathId = pathCounter,
        terminal.node = t, terminal.edge = -1, terminal.cost = 0,
        terminal.aggCost = prefixCost, terminal.aggTime = prefixTime;
        seqCounter++;

        rows.push_back(terminal);

        output.push_back(rows);

        pathCounter++;
    }

    return output;
}

int main()
{

    vector<VertexData> vertices = {{0, 0, 1},
                                   {5, 20, 2},
                                   {6, 10, 3},
                                   {3, 12, 4},
                                   {0, 100, 5}};

    vector<PgEdge> edges = {{1, 1, 3, 1, 5},
                            {2, 2, 2, 2, 5},
                            {3, 2, 4, 1, 2},
                            {4, 2, 5, 2, 7},
                            {5, 3, 2, 7, 3},
                            {6, 3, 4, 3, 8},
                            {7, 4, 5, 1, 3},
                            {8, 5, 1, 1, 5},
                            {9, 5, 2, 1, 4}};

    RCGraph rcg(vertices, edges);
    long long src = 1, target = 5;
    vector<vector<OutputRow>> output = calcShortestDistance(rcg, src, target);

    for (auto path : output)
    {
        for (auto edge : path)
        {
            cout << edge.seq << "\t"
                 << edge.pathId << "\t"
                 << edge.node << "\t"
                 << edge.edge << "\t"
                 << edge.cost << "\t"
                 << edge.aggCost << "\t"
                 << edge.aggTime << "\n";
        }
        cout << "\n\n";
    }

    return 0;
}