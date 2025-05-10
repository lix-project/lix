import logging
from logging import Logger

import pytest
from _pytest.fixtures import FixtureRequest


@pytest.fixture
def logger(request: FixtureRequest) -> Logger:
    """
    Returns a logger object to use in a test function.
    :param request: provide by pytest, information about where this fixture was used
    :return: a logger object to use
    """
    return logging.getLogger(request.function.__name__)
