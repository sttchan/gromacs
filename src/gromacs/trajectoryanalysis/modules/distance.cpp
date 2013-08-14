/*
 * This file is part of the GROMACS molecular simulation package.
 *
 * Copyright (c) 2010,2011,2012,2013, by the GROMACS development team, led by
 * David van der Spoel, Berk Hess, Erik Lindahl, and including many
 * others, as listed in the AUTHORS file in the top-level source
 * directory and at http://www.gromacs.org.
 *
 * GROMACS is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * GROMACS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with GROMACS; if not, see
 * http://www.gnu.org/licenses, or write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 *
 * If you want to redistribute modifications to GROMACS, please
 * consider that scientific software is very special. Version
 * control is crucial - bugs must be traceable. We will be happy to
 * consider code for inclusion in the official distribution, but
 * derived work must not be called official GROMACS. Details are found
 * in the README & COPYING files - if they are missing, get the
 * official version at http://www.gromacs.org.
 *
 * To help us fund GROMACS development, we humbly ask that you cite
 * the research papers on the package. Check out http://www.gromacs.org.
 */
/*! \internal \file
 * \brief
 * Implements gmx::analysismodules::Distance.
 *
 * \author Teemu Murtola <teemu.murtola@gmail.com>
 * \ingroup module_trajectoryanalysis
 */
#include "distance.h"

#include "gromacs/legacyheaders/pbc.h"
#include "gromacs/legacyheaders/vec.h"

#include "gromacs/analysisdata/analysisdata.h"
#include "gromacs/analysisdata/modules/plot.h"
#include "gromacs/options/basicoptions.h"
#include "gromacs/options/filenameoption.h"
#include "gromacs/options/options.h"
#include "gromacs/selection/selection.h"
#include "gromacs/selection/selectionoption.h"
#include "gromacs/trajectoryanalysis/analysissettings.h"
#include "gromacs/utility/exceptions.h"
#include "gromacs/utility/stringutil.h"

namespace gmx
{

namespace analysismodules
{

const char Distance::name[]             = "distance";
const char Distance::shortDescription[] =
    "Calculate distances between pairs of positions";

Distance::Distance()
    : TrajectoryAnalysisModule(name, shortDescription),
      meanLength_(0.1), lengthDev_(1.0), binWidth_(0.001)
{
    averageModule_.reset(new AnalysisDataFrameAverageModule());
    distances_.addModule(averageModule_);
    histogramModule_.reset(new AnalysisDataSimpleHistogramModule());
    distances_.addModule(histogramModule_);

    registerAnalysisDataset(&distances_, "dist");
    registerAnalysisDataset(&xyz_, "xyz");
    registerBasicDataset(averageModule_.get(), "average");
    registerBasicDataset(&histogramModule_->averager(), "histogram");
}


Distance::~Distance()
{
}


void
Distance::initOptions(Options *options, TrajectoryAnalysisSettings * /*settings*/)
{
    static const char *const desc[] = {
        "[TT]gmx distance[tt] calculates distances between pairs of positions",
        "as a function of time. Each selection specifies an independent set",
        "of distances to calculate. Each selection should consist of pairs",
        "of positions, and the distances are computed between positions 1-2,",
        "3-4, etc.[PAR]",
        "[TT]-oav[tt] writes the average distance as a function of time for",
        "each selection.",
        "[TT]-oall[tt] writes all the individual distances.",
        "[TT]-oxyz[tt] does the same, but the x, y, and z components of the",
        "distance are written instead of the norm.",
        "[TT]-oh[tt] writes a histogram of the distances for each selection.",
        "The location of the histogram is set with [TT]-len[tt] and",
        "[TT]-tol[tt]. Bin width is set with [TT]-binw[tt].",
    };

    options->setDescription(concatenateStrings(desc));

    options->addOption(FileNameOption("oav").filetype(eftPlot).outputFile()
                           .store(&fnAverage_).defaultBasename("distave")
                           .description("Average distances as function of time"));
    options->addOption(FileNameOption("oall").filetype(eftPlot).outputFile()
                           .store(&fnAll_).defaultBasename("dist")
                           .description("All distances as function of time"));
    options->addOption(FileNameOption("oxyz").filetype(eftPlot).outputFile()
                           .store(&fnXYZ_).defaultBasename("distxyz")
                           .description("Distance components as function of time"));
    options->addOption(FileNameOption("oh").filetype(eftPlot).outputFile()
                           .store(&fnHistogram_).defaultBasename("disthist")
                           .description("Histogram of the distances"));
    // TODO: Consider what is the best way to support dynamic selections.
    // Again, most of the code already supports it, but it needs to be
    // considered how should -oall work, and additional checks should be added.
    options->addOption(SelectionOption("select").storeVector(&sel_)
                           .required().onlyStatic().multiValue()
                           .description("Position pairs to calculate distances for"));
    // TODO: Extend the histogramming implementation to allow automatic
    // extension of the histograms to cover the data, removing the need for
    // the first two options.
    options->addOption(DoubleOption("len").store(&meanLength_)
                           .description("Mean distance for histogramming"));
    options->addOption(DoubleOption("tol").store(&lengthDev_)
                           .description("Width of full distribution as fraction of [TT]-len[tt]"));
    options->addOption(DoubleOption("binw").store(&binWidth_)
                           .description("Bin width for histogramming"));
}


namespace
{

/*! \brief
 * Checks that selections conform to the expectations of the tool.
 */
void checkSelections(const SelectionList &sel)
{
    for (size_t g = 0; g < sel.size(); ++g)
    {
        if (sel[g].posCount() % 2 != 0)
        {
            std::string message = formatString(
                        "Selection '%s' does not evaluate into an even number of positions "
                        "(there are %d positions)",
                        sel[g].name(), sel[g].posCount());
            GMX_THROW(InconsistentInputError(message));
        }
    }
}

}       // namespace


void
Distance::initAnalysis(const TrajectoryAnalysisSettings &settings,
                       const TopologyInformation         & /*top*/)
{
    checkSelections(sel_);

    distances_.setDataSetCount(sel_.size());
    xyz_.setDataSetCount(sel_.size());
    for (size_t i = 0; i < sel_.size(); ++i)
    {
        const int distCount = sel_[i].posCount() / 2;
        distances_.setColumnCount(i, distCount);
        xyz_.setColumnCount(i, distCount * 3);
    }
    const double histogramMin = (1.0 - lengthDev_) * meanLength_;
    const double histogramMax = (1.0 + lengthDev_) * meanLength_;
    histogramModule_->init(histogramFromRange(histogramMin, histogramMax)
                               .binWidth(binWidth_).includeAll());

    if (!fnAverage_.empty())
    {
        AnalysisDataPlotModulePointer plotm(
                new AnalysisDataPlotModule(settings.plotSettings()));
        plotm->setFileName(fnAverage_);
        plotm->setTitle("Average distance");
        plotm->setXAxisIsTime();
        plotm->setYLabel("Distance (nm)");
        // TODO: Add legends
        averageModule_->addModule(plotm);
    }

    if (!fnAll_.empty())
    {
        AnalysisDataPlotModulePointer plotm(
                new AnalysisDataPlotModule(settings.plotSettings()));
        plotm->setFileName(fnAll_);
        plotm->setTitle("Distance");
        plotm->setXAxisIsTime();
        plotm->setYLabel("Distance (nm)");
        // TODO: Add legends? (there can be a massive amount of columns)
        distances_.addModule(plotm);
    }

    if (!fnXYZ_.empty())
    {
        AnalysisDataPlotModulePointer plotm(
                new AnalysisDataPlotModule(settings.plotSettings()));
        plotm->setFileName(fnAll_);
        plotm->setTitle("Distance");
        plotm->setXAxisIsTime();
        plotm->setYLabel("Distance (nm)");
        // TODO: Add legends? (there can be a massive amount of columns)
        xyz_.addModule(plotm);
    }

    if (!fnHistogram_.empty())
    {
        AnalysisDataPlotModulePointer plotm(
                new AnalysisDataPlotModule(settings.plotSettings()));
        plotm->setFileName(fnHistogram_);
        plotm->setTitle("Distance histogram");
        plotm->setXLabel("Distance (nm)");
        plotm->setYLabel("Probability");
        // TODO: Add legends
        histogramModule_->averager().addModule(plotm);
    }
}


void
Distance::analyzeFrame(int frnr, const t_trxframe &fr, t_pbc *pbc,
                       TrajectoryAnalysisModuleData *pdata)
{
    AnalysisDataHandle   distHandle = pdata->dataHandle(distances_);
    AnalysisDataHandle   xyzHandle  = pdata->dataHandle(xyz_);
    const SelectionList &sel        = pdata->parallelSelections(sel_);

    checkSelections(sel);

    distHandle.startFrame(frnr, fr.time);
    xyzHandle.startFrame(frnr, fr.time);
    for (size_t g = 0; g < sel.size(); ++g)
    {
        distHandle.selectDataSet(g);
        xyzHandle.selectDataSet(g);
        for (int i = 0, n = 0; i < sel[g].posCount(); i += 2, ++n)
        {
            const SelectionPosition &p1 = sel[g].position(i);
            const SelectionPosition &p2 = sel[g].position(i+1);
            rvec                     dx;
            if (pbc != NULL)
            {
                pbc_dx(pbc, p2.x(), p1.x(), dx);
            }
            else
            {
                rvec_sub(p2.x(), p1.x(), dx);
            }
            real dist = norm(dx);
            distHandle.setPoint(n, dist);
            xyzHandle.setPoints(n*3, 3, dx);
        }
    }
    distHandle.finishFrame();
    xyzHandle.finishFrame();
}


void
Distance::finishAnalysis(int /*nframes*/)
{
    AbstractAverageHistogram &averageHistogram = histogramModule_->averager();
    averageHistogram.normalizeProbability();
    averageHistogram.done();
}


void
Distance::writeOutput()
{
    // TODO: Print bond length statistics
    //fprintf(stderr, "Average distance: %f\n", avem_->average(0));
    //fprintf(stderr, "Std. deviation:   %f\n", avem_->stddev(0));
}

} // namespace analysismodules

} // namespace gmx