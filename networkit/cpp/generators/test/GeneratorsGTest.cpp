/*
Dy * GeneratorsTest.cpp
 *
 *  Created on: 09.04.2013
 *      Author: cls
 */

#ifndef NOGTEST

#include <numeric>

#include "GeneratorsGTest.h"

#include "../DynamicPathGenerator.h"
#include "../DynamicForestFireGenerator.h"
#include "../DynamicDorogovtsevMendesGenerator.h"
#include "../DorogovtsevMendesGenerator.h"
#include "../WattsStrogatzGenerator.h"
#include "../RegularRingLatticeGenerator.h"
#include "../../properties/ClusteringCoefficient.h"
#include "../../community/PLM.h"
#include "../../community/Modularity.h"
#include "../StochasticBlockmodel.h"
#include "../ConfigurationModelGenerator.h"


namespace NetworKit {

GeneratorsGTest::GeneratorsGTest() {

}

TEST_F(GeneratorsGTest, testDynamicBarabasiAlbertGeneratorSingleStep) {
	count k = 2; // number of edges added per node
	DynamicGraphSource* gen = new DynamicBarabasiAlbertGenerator(k);
	GraphEventProxy* Gproxy = gen->newGraph();
	Graph* G = Gproxy->G;

	gen->initializeGraph();

	count nPre = G->numberOfNodes();
	count mPre = G->numberOfEdges();
	EXPECT_EQ(k, nPre) << "graph should have been initialized to k nodes";
	EXPECT_EQ(k - 1, mPre) << "graph should have been initialized to a path of k nodes which means k-1 edges";

	// perform single preferential attachment step
	gen->generate();

	count nPost = G->numberOfNodes();
	count mPost = G->numberOfEdges();
	EXPECT_EQ(nPre + 1, nPost) << "one more node should have been added";
	EXPECT_EQ(mPre + k, mPost) << "k edges should have been added";

	delete G;
}

TEST_F(GeneratorsGTest, testDynamicBarabasiAlbertGenerator) {


	DynamicGraphSource* gen = new DynamicBarabasiAlbertGenerator(2);

	GraphEventProxy* Gproxy = gen->newGraph();
	Graph* G = Gproxy->G;

	gen->initializeGraph();

	EXPECT_EQ(2u, G->numberOfNodes()) << "initially the generator creates two connected nodes";
	EXPECT_EQ(1u, G->numberOfEdges()) << "initially the generator creates two connected nodes";

	count n = 100;

	gen->generateWhile([&]() {
				return ( G->numberOfNodes() < n );
			});

	EXPECT_EQ(n, G->numberOfNodes());
	DEBUG("m = " , G->numberOfEdges());

	// resume generator

	gen->generateWhile([&]() {
		return (G->numberOfNodes() < 2 * n);
	});
	EXPECT_EQ(2 * n, G->numberOfNodes());
}


TEST_F(GeneratorsGTest, viewDynamicBarabasiAlbertGenerator) {
	DynamicGraphSource* gen = new DynamicBarabasiAlbertGenerator(2);
	GraphEventProxy* Gproxy = gen->newGraph();
	Graph* G = Gproxy->G;
	gen->initializeGraph();
	count n = 42;
	gen->generateWhile([&]() {
				return ( G->numberOfNodes() < n );
			});
	METISGraphWriter writer;
	writer.write(*G, "output/BATest.graph");

	delete G;
}


TEST_F(GeneratorsGTest, testStaticPubWebGenerator) {
	count n = 1800;
	count numCluster = 24;
	count maxNumNeighbors = 36;
	float rad = 0.075;

	PubWebGenerator gen(n, numCluster, rad, maxNumNeighbors);
	Graph G = gen.generate();
	EXPECT_EQ(n, G.numberOfNodes()) << "number of generated nodes";

	// check degree
	G.forNodes([&](node v) {
		EXPECT_LE(G.degree(v), maxNumNeighbors) << "maximum degree";
	});

	// 1-clustering
	ClusteringGenerator clusterGen;
	Partition oneClustering = clusterGen.makeOneClustering(G);
	EXPECT_EQ(G.numberOfNodes(),oneClustering.numberOfElements());

	// output to EPS file
	PostscriptWriter psWriter(true);
	psWriter.write(G, oneClustering, "output/pubweb.eps");

	// clustering
	PLM clusterAlgo(G);
	clusterAlgo.run();
	Partition clustering = clusterAlgo.getPartition();
	EXPECT_EQ(G.numberOfNodes(),clustering.numberOfElements());
	psWriter.write(G, clustering, "output/pubweb-clustered-PLM.eps");

	Modularity mod;
	double modVal = mod.getQuality(clustering, G);
	EXPECT_GE(modVal, 0.2) << "modularity of clustering";
	DEBUG("Modularity of clustering: " , modVal);
	DEBUG("Total edge weight: " , G.totalEdgeWeight());
	EXPECT_TRUE(G.checkConsistency());
}


TEST_F(GeneratorsGTest, testDynamicPubWebGenerator) {
//	count nSteps = 100;
//	count n = 1200;
	count nSteps = 15;
	count n = 300;
	count numCluster = 30;
	count maxNumNeighbors = 40;
	float rad = 0.08;

	DynamicPubWebGenerator dynGen(n, numCluster, rad, maxNumNeighbors, false);
	Graph G = dynGen.getGraph();
	GraphUpdater gu(G);
	std::vector<GraphEvent> stream;

	// static clustering algorithm for better visual output
	PostscriptWriter psWriter(true);
	psWriter.write(G, "output/pubweb-0000.eps");

	for (index i = 1; i <= nSteps; ++i) {
		stream = dynGen.generate(1);
		DEBUG("updating graph");
		gu.update(stream);
		G.initCoordinates();

		DEBUG("updated graph, new (n, m) = (" , G.numberOfNodes() , ", " , G.numberOfEdges() , ")");
		edgeweight tew = G.totalEdgeWeight();
		DEBUG("1/2 graph volume: ", tew);
		EXPECT_GT(tew, 0);

		// update coordinates
		std::map<node, Point<float> > newCoordinates = dynGen.getNewCoordinates();
		for (std::map<node, Point<float> >::iterator iter = newCoordinates.begin();
				iter != newCoordinates.end(); ++iter) {
			node v = iter->first;
			Point<float> p = iter->second;
			G.setCoordinate(v, p);
		}

		// output for visual inspection
		char path[23];
		sprintf(path, "output/pubweb-%04llu.eps", static_cast<unsigned long long>(i));
		TRACE("path: " , path);
		psWriter.write(G, path);
	}
}

/**
 * Testing the dynamic hyperbolic generator with fixed node positions and a growing distance threshold.
 */
TEST_F(GeneratorsGTest, testDynamicHyperbolicGeneratorOnFactorGrowth) {
	//set up dynamic parameters
	int nSteps = 20;
	count n = 1000;
	double initialFactor = 0.5;
	double factorGrowth = (double) (1 - initialFactor) / nSteps;

	//set up node positions
	double stretch = 1;
	double alpha = 1;
	double R = HyperbolicSpace::hyperbolicAreaToRadius(n)*stretch;
	vector<double> angles(n, -1);
	vector<double> radii(n, -1);
	HyperbolicSpace::fillPoints(angles, radii, stretch, alpha);
	double r = HyperbolicSpace::hyperbolicRadiusToEuclidean(R);

	//set up generators
	DynamicHyperbolicGenerator dynGen(angles, radii, stretch, initialFactor, 0, factorGrowth, 0);

	Graph G = dynGen.getGraph();
	GraphUpdater gu(G);
	std::vector<GraphEvent> stream;

	for (int i = 0; i < nSteps; i++) {
		stream = dynGen.generate(1);
		for (auto event : stream) {
			//the query disk is growing, no edges should be removed
			EXPECT_NE(event.type, GraphEvent::EDGE_REMOVAL);
			EXPECT_TRUE(event.type == GraphEvent::EDGE_ADDITION || event.type == GraphEvent::TIME_STEP);
			if (event.type == GraphEvent::EDGE_ADDITION) {
				//nodes connected by the new edges should have a distance between the current threshold and the previous one
				double distance = HyperbolicSpace::poincareMetric(angles[event.u], radii[event.u], angles[event.v], radii[event.v]);
				EXPECT_GE(distance, (initialFactor+factorGrowth*i)*R);
				EXPECT_LE(distance, (initialFactor+factorGrowth*(i+1))*R);
			}
		}
		//the graph recreated from the edge stream should be consistent
		gu.update(stream);
		EXPECT_TRUE(G.checkConsistency());
	}
	//graph recreated from edge stream should be equal to graph generated with final parameters
	Graph comparison = HyperbolicGenerator().generate(angles, radii, r, R);
	EXPECT_EQ(G.numberOfEdges(), comparison.numberOfEdges());
}


/**
 * Testing the dynamic hyperbolic generator with fixed parameters and changing node positions
 */
TEST_F(GeneratorsGTest, testDynamicHyperbolicGeneratorOnMovedNodes) {
	//set up dynamic parameters
	int nSteps = 20;
	count n = 1000;

	double factor = 1;
	double stretch = 2;
	double alpha = 1;
	double R = HyperbolicSpace::hyperbolicAreaToRadius(n)*stretch;
	double movedShare = 1;
	double moveDistance = 0.1;

	//set up initial node positions
	vector<double> angles(n, -1);
	vector<double> radii(n, -1);
	HyperbolicSpace::fillPoints(angles, radii, stretch, alpha);
	double r = HyperbolicSpace::hyperbolicRadiusToEuclidean(R);

	DynamicHyperbolicGenerator dynGen(angles, radii, stretch, factor, movedShare, 0, moveDistance);

	//generate starting graph
	Graph G = HyperbolicGenerator().generate(angles, radii, r, factor*R);
	count initialEdgeCount = G.numberOfEdges();
	GraphUpdater gu(G);
	std::vector<GraphEvent> stream;

	for (int i = 0; i < nSteps; i++) {
		//move nodes and generate stream of affected edges
		stream = dynGen.generate(1);
		DEBUG("Edges: ", G.numberOfEdges());
		for (auto event : stream) {
			EXPECT_TRUE(event.type == GraphEvent::EDGE_REMOVAL || event.type == GraphEvent::EDGE_ADDITION || event.type == GraphEvent::TIME_STEP);
			if (event.type == GraphEvent::EDGE_REMOVAL) {
				EXPECT_TRUE(G.hasEdge(event.u, event.v));
			}
			//only present nodes can be affected, no new nodes are introduced
			if (event.type != GraphEvent::TIME_STEP) EXPECT_LT(event.u, G.upperNodeIdBound());
		}
		gu.update(stream);
		EXPECT_TRUE(G.checkConsistency());
	}

	//update moved nodes
	angles = getAngles(dynGen);
	radii = getRadii(dynGen);
	Graph comparison = HyperbolicGenerator().generate(angles, radii, r, R*factor);
	EXPECT_EQ(G.numberOfEdges(), comparison.numberOfEdges());

	//heuristic criterion: Number of edges may change, but should not change much
	EXPECT_NEAR(G.numberOfEdges(), initialEdgeCount, initialEdgeCount/10);
}

/**
 * creates a series of pictures visualizing the effect of the dynamic hyperbolic generator
 */
TEST_F(GeneratorsGTest, testDynamicHyperbolicVisualization) {
	count n = 300;
	count nSteps = 20;

	double factor = 0.5;
	double stretch = 1;
	double alpha = 1;
	double movedShare = 0.2;
	double moveDistance = 1;
	vector<double> angles(n);
	vector<double> radii(n);

	HyperbolicSpace::fillPoints(angles, radii, stretch, alpha);

	DynamicHyperbolicGenerator dynGen(angles, radii, stretch, factor, movedShare, 0, moveDistance);
	Graph G = dynGen.getGraph();

	GraphUpdater gu(G);
	std::vector<GraphEvent> stream;
	G.initCoordinates();
	PostscriptWriter psWriter(true);
	psWriter.write(G, "output/hyperbolic-0000.eps");

	for (index i = 0; i < nSteps; i++) {
		stream = dynGen.generate(1);
		DEBUG("Edges: ", G.numberOfEdges());
		for (auto event : stream) {
			EXPECT_TRUE(event.type == GraphEvent::EDGE_REMOVAL || event.type == GraphEvent::EDGE_ADDITION || event.type == GraphEvent::TIME_STEP);
		}
		gu.update(stream);
		G.initCoordinates();

		auto coords = dynGen.getHyperbolicCoordinates();
		for (index j = 0; j < coords.size(); j++) {
			G.setCoordinate(j, coords[j]);
		}

		// output for visual inspection
		char path[27];//TODO: come on, this is ridiculous!
		sprintf(path, "output/hyperbolic-%04llu.eps", static_cast<unsigned long long>(i));
		TRACE("path: " , path);
		psWriter.write(G, path);
	}
}

/**
 * When using a dynamic graph generator, generating many time steps at once should have the same output as calling the generation method for one step multiple times.
 */
TEST_F(GeneratorsGTest, testDynamicHyperbolicGeneratorCollectedSteps) {
	count n = 10;
	count nSteps = 100;

	double stretch = 1;
	double alpha = 1;
	double R = HyperbolicSpace::hyperbolicAreaToRadius(n)*stretch;
	double initialFactor = 0;
	double factorGrowth = (double) (1 - initialFactor) / nSteps;

	vector<double> angles(n, -1);
	vector<double> radii(n, -1);
	HyperbolicSpace::fillPoints(angles, radii, stretch, alpha);

	DynamicHyperbolicGenerator dyngen(angles, radii, R, initialFactor, 0, factorGrowth, 0);

	DynamicHyperbolicGenerator copy(angles, radii, R, initialFactor, 0, factorGrowth, 0);
	std::vector<GraphEvent> stream;

	//generate steps one at a time
	for (index i = 0; i < nSteps; i++) {
		std::vector<GraphEvent> stepStream = dyngen.generate(1);
		stream.insert(stream.end(), stepStream.begin(), stepStream.end());
	}

	//generate steps all at once
	std::vector<GraphEvent> comparison = copy.generate(nSteps);
	EXPECT_EQ(stream.size(), comparison.size());

	//sort graph events to compare them
	std::sort(stream.begin(), stream.end(), GraphEvent::compare);
	std::sort(comparison.begin(), comparison.end(), GraphEvent::compare);
	vector<GraphEvent> diff(stream.size()+comparison.size());
	auto newend = std::set_difference(stream.begin(), stream.end(), comparison.begin(), comparison.end(), diff.begin(), GraphEvent::equal);
	diff.resize(newend - diff.begin());
	for (auto event : diff) {
		DEBUG("Found ", event.toString(), " in one but not other.");
	}
	if (diff.size() > 0) {
		DEBUG("G:");
		for (auto orig : stream) {
			DEBUG(orig.toString());
		}
		DEBUG("Comparison:");
		for (auto orig : comparison) {
			DEBUG(orig.toString());
		}
	}
	EXPECT_TRUE(std::equal(stream.begin(), stream.end(), comparison.begin(), GraphEvent::equal));
}

TEST_F(GeneratorsGTest, testBarabasiAlbertGenerator) {
	count k = 3;
	count nMax = 100;
	count n0 = 3;

	BarabasiAlbertGenerator BarabasiAlbert(k, nMax, n0);
	Graph G(0);
	EXPECT_TRUE(G.isEmpty());

	G = BarabasiAlbert.generate();
	EXPECT_FALSE(G.isEmpty());

	EXPECT_EQ(nMax, G.numberOfNodes());
	EXPECT_EQ( ((n0-1) + ((nMax - n0) * k)), G.numberOfEdges());
	EXPECT_TRUE(G.checkConsistency());
}

TEST_F(GeneratorsGTest, generatetBarabasiAlbertGeneratorGraph) {
		count k = 3;
		count nMax = 1000;
		count n0 = 3;

		BarabasiAlbertGenerator BarabasiAlbert(k, nMax, n0);

		Graph G = BarabasiAlbert.generate();
		GraphIO io;
		io.writeAdjacencyList(G, "output/"
				"BarabasiGraph.txt");
}

TEST_F(GeneratorsGTest, testDynamicPathGenerator) {
	DynamicPathGenerator gen;
	auto stream = gen.generate(42);
	for (auto ev : stream) {
		TRACE(ev.toString());
	}
}

TEST_F(GeneratorsGTest, testErdosRenyiGenerator) {
	count n = 2000;
	double p = 1.5 * (log(n) / (double) n);

	ErdosRenyiGenerator generator(n, p);
	Graph G = generator.generate();
	EXPECT_EQ(n, G.numberOfNodes());
	EXPECT_FALSE(G.isEmpty());
	EXPECT_TRUE(G.checkConsistency());

	count nPairs = (n * (n-1)) / 2;
	count nEdges = G.numberOfEdges();
	EXPECT_GE(nEdges, 0.75 * p * nPairs);
	EXPECT_LE(nEdges, 1.25 * p * nPairs);

	DEBUG("Number of edges with probability " , p , " (actual/expected): " , nEdges , " / " , (nPairs * p));
	EXPECT_TRUE(G.checkConsistency());
}

TEST_F(GeneratorsGTest, testRmatGeneratorException) {
	count scale = 9;
	count edgeFactor = 12;
	double a = 0.51;
	double b = 0.12;
	double c = 0.12;
	double d = 0.2;

	EXPECT_THROW(RmatGenerator rmat(scale, edgeFactor, a, b, c, d), std::runtime_error);
}

TEST_F(GeneratorsGTest, testRmatGenerator) {
	count scale = 9;
	count n = (1 << scale);
	count edgeFactor = 12;
	double a = 0.51;
	double b = 0.12;
	double c = 0.12;
	double d = 0.25;

	RmatGenerator rmat(scale, edgeFactor, a, b, c, d);
	Graph G = rmat.generate();

	EXPECT_EQ(G.numberOfNodes(), n);
	EXPECT_LE(G.numberOfEdges(), n * edgeFactor);

	ClusteringCoefficient cc;
	double ccex = cc.exactGlobal(G);
	EXPECT_LE(ccex, 0.4);

	PLM clusterer(G, true);
	clusterer.run();
	Partition zeta = clusterer.getPartition();
	Modularity mod;
	double modVal = mod.getQuality(zeta, G);
	INFO("Modularity of R-MAT graph clustering: ", modVal);
	EXPECT_GE(modVal, 0.0);
	EXPECT_TRUE(G.checkConsistency());
}


TEST_F(GeneratorsGTest, testChungLuGenerator) {
	count n = 400;
	count maxDegree = n / 8;
	std::vector<count> sequence(n);
	count expVolume = 0;
	count actualVolume = 0;

	// fill sequence with random values (this is not power-law, of course!)
	for (index i = 0; i < n; ++i) {
		sequence[i] = rand() % maxDegree;
		expVolume += sequence[i];
	}

	ChungLuGenerator gen(sequence);
	Graph G = gen.generate();
	EXPECT_TRUE(G.checkConsistency());

	EXPECT_EQ(n, G.numberOfNodes());
	G.forNodes([&](node v) {
		actualVolume += G.degree(v);
	});

	INFO("expected volume: ", expVolume, ", actual volume: ", actualVolume);
}

TEST_F(GeneratorsGTest, testHavelHakimiGeneratorOnRandomSequence) {
	count n = 400;
	count maxDegree = n / 10;
	std::vector<count> sequence(n);
//	std::vector<count> sequence = {5, 4, 4, 3, 2, 2, 2, 2, 2, 2};
	bool realizable = false;

	do {
		// fill sequence with random values (this is not power-law, of course!)
		for (index i = 0; i < n; ++i) {
			sequence[i] = rand() % maxDegree;
		}

		// check if sequence is realizable
		HavelHakimiGenerator hhgen(sequence);
		realizable = hhgen.isRealizable();

		if (realizable) {
			Graph G = hhgen.generate();
			EXPECT_TRUE(G.checkConsistency());
			count volume = std::accumulate(sequence.begin(), sequence.end(), 0);
			EXPECT_EQ(volume, 2 * G.numberOfEdges());
		}
	} while (! realizable);
}

TEST_F(GeneratorsGTest, testHavelHakimiGeneratorOnRealSequence) {
	METISGraphReader reader;
	std::vector<std::string> graphs = {"input/jazz.graph",
			"input/lesmis.graph"}; //, "input/PGPgiantcompo.graph", "input/coAuthorsDBLP.graph"};

	for (auto path : graphs) {
		Graph G = reader.read(path);
		count n = G.numberOfNodes();
		std::vector<count> sequence = GraphProperties::degreeSequence(G);

		HavelHakimiGenerator hhgen(sequence);
		Graph G2 = hhgen.generate();
		EXPECT_TRUE(G.checkConsistency());

		count volume = std::accumulate(sequence.begin(), sequence.end(), 0);
		EXPECT_EQ(volume, 2 * G2.numberOfEdges());

		if (volume < 50000) {
			std::vector<count> testSequence = GraphProperties::degreeSequence(G2);

			for (index i = 0; i < n; ++i) {
				EXPECT_EQ(sequence[i], testSequence[i]);
			}
		}
	}
}

TEST_F(GeneratorsGTest, testHavelHakimiGeneratorOnUnrealizableSequence) {
	std::vector<count> seq = {20, 10, 2, 2, 2, 2, 2, 2, 2, 2, 2};

	HavelHakimiGenerator hhgen(seq);
	EXPECT_THROW(hhgen.generate(), std::runtime_error);

	hhgen = HavelHakimiGenerator(seq, true);
	Graph G = hhgen.generate();

	G.forNodes([&](node u) {
		EXPECT_EQ(std::min<count>(seq[u], 10), G.degree(u));
	});
}


TEST_F(GeneratorsGTest, testDynamicForestFireGenerator) {
	Graph G1(0);
	GraphUpdater gu1(G1);
	std::vector<GraphEvent> stream;
	DynamicForestFireGenerator ffg1(0.0, false);
	stream = ffg1.generate(10);
	gu1.update(stream);
	EXPECT_TRUE(G1.checkConsistency());
	EXPECT_EQ(10u, G1.numberOfNodes());
	G1.forNodes([&](node u) {
		count c = 0;
		G1.forNeighborsOf(u, [&](node v) {
			if (v < u) {
				c += 1;
			}
		});
		if (u == 0) {
			EXPECT_EQ(0u, c);
		} else {
			EXPECT_EQ(1u, c);
		}
	});

	Graph G2(0);
	GraphUpdater gu2(G2);
	DynamicForestFireGenerator ffg2(1.0, true, 1.0);
	stream = ffg2.generate(10);
	gu2.update(stream);
	EXPECT_TRUE(G2.checkConsistency());
	EXPECT_EQ(10u, G2.numberOfNodes());
	G2.forNodePairs([&](node u, node v) {
		if (v < u) {
			EXPECT_TRUE(G2.hasEdge(u,v));
		}
	});
	stream = ffg2.generate(10);
	gu2.update(stream);
	EXPECT_EQ(20u, G2.numberOfNodes());
}

TEST_F(GeneratorsGTest, testRegularRingLatticeGenerator) {
	int n0 = 10;
	int neighbors = 2;
	auto testRingLattice = [&](Graph G) {
		EXPECT_EQ(n0, (int) G.numberOfNodes());
		EXPECT_EQ(n0 * neighbors, (int) G.numberOfEdges());
		G.forNodePairs([&](node u, node v) {
			int diff = std::abs((int) u- (int) v);
			if (u != v && (diff <= neighbors || diff >= n0 - neighbors)) {
				EXPECT_TRUE(G.hasEdge(u,v));
			} else {
				EXPECT_FALSE(G.hasEdge(u,v));
			}
		});
	};

	RegularRingLatticeGenerator rrlg = RegularRingLatticeGenerator(n0, neighbors);
	testRingLattice(rrlg.generate());
}

TEST_F(GeneratorsGTest, testWattsStrogatzGenerator) {
	int n0 = 10;
	int neighbors = 2;
	auto testRingLattice = [&](Graph G) {
		G.forNodePairs([&](node u, node v) {
			int diff = std::abs((int) u- (int) v);
			if (u != v && (diff <= neighbors || diff >= n0 - neighbors)) {
				EXPECT_TRUE(G.hasEdge(u,v));
			} else {
				EXPECT_FALSE(G.hasEdge(u,v));
			}
		});
	};

	WattsStrogatzGenerator wsg1 = WattsStrogatzGenerator(n0, neighbors, 0.0);
	testRingLattice(wsg1.generate());

	WattsStrogatzGenerator wsg2 = WattsStrogatzGenerator(n0, neighbors, 0.3);
	Graph G = wsg2.generate();
	EXPECT_TRUE(G.checkConsistency());
	EXPECT_EQ(n0, (int) G.numberOfNodes());
	EXPECT_EQ(n0*neighbors, (int) G.numberOfEdges());
}

TEST_F(GeneratorsGTest, testDorogovtsevMendesGenerator) {
	int n0 = 20;
	DorogovtsevMendesGenerator dmg = DorogovtsevMendesGenerator(n0);
	Graph G = dmg.generate();

	EXPECT_EQ(n0, (int) G.numberOfNodes());
	EXPECT_EQ(2 * n0 - 3, (int) G.numberOfEdges());
	G.forNodes([&](node u) {
		count c = 0;
		G.forNeighborsOf(u, [&](node v) {
			if (v < u) {
				c += 1;
			}
		});
		if (u <= 2) {
			EXPECT_EQ(u, c);
		} else {
			EXPECT_EQ(2u, c);
		}
	});
	EXPECT_TRUE(G.checkConsistency());
}

TEST_F(GeneratorsGTest, testDynamicDorogovtsevMendesGenerator) {
	count n0 = 20;
	DynamicDorogovtsevMendesGenerator ddmg = DynamicDorogovtsevMendesGenerator();
	Graph G(0);
	GraphUpdater gu(G);
	std::vector<GraphEvent> stream;
	stream = ddmg.generate(n0 - 3);
	gu.update(stream);

	EXPECT_EQ(n0, G.numberOfNodes());
	EXPECT_EQ(2*n0-3, G.numberOfEdges());
	G.forNodes([&](node u) {
		count c = 0;
		G.forNeighborsOf(u, [&](node v) {
			if (v < u) {
				c += 1;
			}
		});
		if (u <= 2) {
			EXPECT_EQ(u, c);
		} else {
			EXPECT_EQ(2u, c);
		}
	});
}



TEST_F(GeneratorsGTest, testStochasticBlockmodel) {
	count n = 10;
	count nBlocks = 2;
	std::vector<index> membership = {0, 0, 0, 0, 0, 1, 1, 1, 1, 1};
	std::vector<std::vector<double> > affinity = {{1.0, 0.0}, {0.0, 1.0}};
	StochasticBlockmodel sbm(n, nBlocks, membership, affinity);
	Graph G = sbm.generate();

	EXPECT_EQ(n, G.numberOfNodes());
	EXPECT_EQ(20u, G.numberOfEdges());
}

/**
 * Test whether points generated in hyperbolic space fulfill basic constraints
 */
TEST_F(GeneratorsGTest, testHyperbolicPointGeneration) {
	count n = 1000;
	double stretch = Aux::Random::real(0.5,1.5);
	double alpha = Aux::Random::real(0.5,1.5);
	double R = HyperbolicSpace::hyperbolicAreaToRadius(n)*stretch;
	vector<double> angles(n, -1);
	vector<double> radii(n, -1);
	HyperbolicSpace::fillPoints(angles, radii, stretch, alpha);
	for (index i = 0; i < n; i++) {
		EXPECT_GE(angles[i], 0);
		EXPECT_LT(angles[i], 2*M_PI);
		EXPECT_GE(radii[i], 0);
		EXPECT_LE(radii[i], HyperbolicSpace::hyperbolicRadiusToEuclidean(R));
	}
}

/**
 * Test whether the number edges generated by the hyperbolic generator agree at least roughly with theory
 */
TEST_F(GeneratorsGTest, testHyperbolicGenerator) {
	count n = 100000;
	double k = 32;
	count m = k*n/2;
	HyperbolicGenerator gen(n,k,7);
	Graph G = gen.generate();
	EXPECT_EQ(G.numberOfNodes(), n);
	EXPECT_TRUE(G.checkConsistency());
	EXPECT_NEAR(G.numberOfEdges(), m, m/10);
}

TEST_F(GeneratorsGTest, testHyperbolicGeneratorWithSequentialQuadtree) {
	count n = 10000;
	double s = 1.2;
	double t = 1;
	double alpha =1;

	vector<double> angles(n);
	vector<double> radii(n);
	HyperbolicSpace::fillPoints(angles, radii, s, alpha);
	double R = s*HyperbolicSpace::hyperbolicAreaToRadius(n);
	double r = HyperbolicSpace::hyperbolicRadiusToEuclidean(R);
	Quadtree<index> quad(r);

	for (index i = 0; i < n; i++) {
		quad.addContent(i, angles[i], radii[i]);
	}

	angles.clear();
	radii.clear();

	quad.trim();
	quad.sortPointsInLeaves();
	quad.reindex();
	quad.extractCoordinates(angles, radii);

	HyperbolicGenerator gen;
	Graph G = gen.generate(angles, radii, quad, t*R);
	count expected = HyperbolicGenerator::expectedNumberOfEdges(n,s);
	EXPECT_EQ(n, G.numberOfNodes());
	EXPECT_NEAR(G.numberOfEdges(), expected, expected/10);
	EXPECT_TRUE(G.checkConsistency());
}

TEST_F(GeneratorsGTest, testHyperbolicGeneratorWithDataFromParallelQuadtree) {
	count n = 10000;
	double s = 1.2;
	double t = 1;

	Quadtree<index> quad(n,s);
	vector<double> angles;
	vector<double> radii;
	quad.trim();
	quad.sortPointsInLeaves();
	quad.reindex();
	quad.extractCoordinates(angles, radii);
	EXPECT_EQ(angles.size(), n);
	EXPECT_EQ(radii.size(), n);

	vector<index> elements = quad.getElements();
	EXPECT_EQ(elements.size(), n);
	for (index i = 0; i < elements.size(); i++) {
		EXPECT_EQ(elements[i], i);
	}

	double R = s*HyperbolicSpace::hyperbolicAreaToRadius(n);
	double r = HyperbolicSpace::hyperbolicRadiusToEuclidean(R);

	HyperbolicGenerator gen;
	Graph G = gen.generate(angles, radii, r, t*R);
	count expected = HyperbolicGenerator::expectedNumberOfEdges(n,s);
	EXPECT_EQ(n, G.numberOfNodes());
	EXPECT_NEAR(G.numberOfEdges(), expected, expected/10);
	EXPECT_TRUE(G.checkConsistency());
}

TEST_F(GeneratorsGTest, testHyperbolicGeneratorWithParallelQuadtree) {
	count n = 10000;
	double s = 1.2;
	double t = 1;
	count oldthreads = omp_get_max_threads();
	omp_set_num_threads(8);

	Quadtree<index> quad(n,s);
	vector<double> angles;
	vector<double> radii;
	quad.trim();
	quad.sortPointsInLeaves();
	quad.reindex();
	quad.extractCoordinates(angles, radii);
	EXPECT_EQ(angles.size(), n);
	EXPECT_EQ(radii.size(), n);

	vector<index> elements = quad.getElements();
	EXPECT_EQ(elements.size(), n);
	for (index i = 0; i < elements.size(); i++) {
		EXPECT_EQ(elements[i], i);
	}

	double R = s*HyperbolicSpace::hyperbolicAreaToRadius(n);

	HyperbolicGenerator gen;
	Graph G = gen.generate(angles, radii, quad, t*R);
	count expected = HyperbolicGenerator::expectedNumberOfEdges(n,s);
	EXPECT_EQ(n, G.numberOfNodes());
	EXPECT_NEAR(G.numberOfEdges(), expected, expected/10);
	EXPECT_TRUE(G.checkConsistency());
	omp_set_num_threads(oldthreads);
}

/**
 * Check consistency of graphs generated by the hyperbolic generator
 */
TEST_F(GeneratorsGTest, testHyperbolicGeneratorConsistency) {
	count n = 10000;
	double k = 6;
	count m = n*k/2;
	HyperbolicGenerator gen(n, k);
	Graph G = gen.generate();
	EXPECT_NEAR(G.numberOfEdges(), m, m/10);
	ASSERT_TRUE(G.checkConsistency());
}

TEST_F(GeneratorsGTest, testConfigurationModelGeneratorOnRealSequence) {
	METISGraphReader reader;
	std::vector<std::string> graphs = {"input/jazz.graph",
			"input/lesmis.graph"}; //, "input/PGPgiantcompo.graph", "input/coAuthorsDBLP.graph"};

	for (auto path : graphs) {
		Graph G = reader.read(path);
		count n = G.numberOfNodes();
		std::vector<count> sequence = GraphProperties::degreeSequence(G);

		bool skipTest = false;
		ConfigurationModelGenerator gen(sequence, skipTest);
		Graph G2 = gen.generate();

		count volume = std::accumulate(sequence.begin(), sequence.end(), 0);
		EXPECT_EQ(volume, 2 * G2.numberOfEdges());

		if (volume < 50000) {
			std::vector<count> testSequence = GraphProperties::degreeSequence(G2);
			std::sort(testSequence.begin(), testSequence.end(), std::greater<count>());
			std::sort(sequence.begin(), sequence.end(), std::greater<count>());

			for (index i = 0; i < n; ++i) {
				EXPECT_EQ(sequence[i], testSequence[i]);
			}
		}
	}
}

TEST_F(GeneratorsGTest, tryGiganticCollectionOfHyperbolicUnitDiskGraphs) {
	count n = 1000000;
	double k = 1;
	for (index i = 0; i < 7; i++) {
		count m = n*k/2;
		HyperbolicGenerator gen(n, k, 7);
		Graph G = gen.generate();
		EXPECT_NEAR(G.numberOfEdges(), m, m/10);
		EXPECT_TRUE(G.checkConsistency());
		k *= 2;
	}
}

} /* namespace NetworKit */

#endif /*NOGTEST */
